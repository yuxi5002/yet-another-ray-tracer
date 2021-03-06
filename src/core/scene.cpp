#include <yart/core/scene.h>
#include <yart/core/device.h>
#include <yart/core/geometry.h>
#include <yart/core/material.h>
#include <yart/core/camera.h>
#include <random>
#include <algorithm>
#include "../util/macro.h"
#include "../util/rayhit.h"

namespace yart
{

Scene::Scene(const Device& device) : _need_commit(true)
{
    _raw = rtcNewScene(device._raw);
}

Scene::~Scene()
{
    rtcReleaseScene(_raw);
}

void Scene::add(Geometry& geom, Material& mat)
{
    geom._id = rtcAttachGeometry(_raw, geom._raw);
    _mats.push_back(&mat);
    _need_commit = true;
}

unsigned Scene::render(const Camera& camera,
                       unsigned char* pixels,
                       int width,
                       int height,
                       int samples,
                       bool interleaved,
                       int depth)
{
    if (_need_commit) {
        rtcCommitScene(_raw);
        _need_commit = false;
    }

    RTCIntersectContext context;
    rtcInitIntersectContext(&context);

    std::random_device rd;
    std::minstd_rand rd_gen(rd());
    std::uniform_real_distribution<> rd_number(0.0f, 1.0f);

    unsigned num_rays = 0;
#pragma omp parallel for schedule(dynamic) reduction(+ : num_rays)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Eigen::Array3f irradiance(0.0f, 0.0f, 0.0f);
            for (int s = 0; s < samples; ++s) {
                auto u = static_cast<float>(x + rd_number(rd_gen)) / width;
                auto v = static_cast<float>(y + rd_number(rd_gen)) / height;
                auto rayhit = camera.gen_ray(u, v);
                Eigen::Array3f ir;
                num_rays += _path_tracing(context, rayhit, ir, depth);
                irradiance += ir;
            }

            irradiance /= samples;
            const float gamma = 1.0f / 2.2f;
            irradiance = irradiance.pow(gamma);
            irradiance *= 255.0f;
            auto r = std::min(static_cast<int>(irradiance(0)), 255);
            auto g = std::min(static_cast<int>(irradiance(1)), 255);
            auto b = std::min(static_cast<int>(irradiance(2)), 255);
            if (interleaved) {
                pixels[3 * ((height - y - 1) * width + x) + 0] =
                    static_cast<unsigned char>(r);
                pixels[3 * ((height - y - 1) * width + x) + 1] =
                    static_cast<unsigned char>(g);
                pixels[3 * ((height - y - 1) * width + x) + 2] =
                    static_cast<unsigned char>(b);
            }
            else {
                pixels[(height - y - 1) * width + x] =
                    static_cast<unsigned char>(r);
                pixels[width * height + (height - y - 1) * width + x] =
                    static_cast<unsigned char>(g);
                pixels[2 * width * height + (height - y - 1) * width + x] =
                    static_cast<unsigned char>(b);
            }
        }
    }
    return num_rays;
}

unsigned Scene::_path_tracing(RTCIntersectContext& context,
                              RTCRayHit& rayhit,
                              Eigen::Array3f& irradiance,
                              unsigned depth)
{
    rtcIntersect1(_raw, &context, &rayhit);
    if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID) {
        // // Debug: visualize normals
        // Eigen::Vector3f color(
        //     rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z);
        // color += Eigen::Vector3f::Ones();
        // color *= 0.5f;
        // return color;

        Eigen::Vector3f scattered;
        Eigen::Array3f attenuation;
        if (depth > 0 &&
            _mats[rayhit.hit.geomID]->scatter(rayhit, scattered, attenuation)) {
            auto hitpt = get_hitpt(rayhit);
            auto secondary = make_rayhit(hitpt, scattered, 0.0001f);
            Eigen::Array3f traced;
            auto num_bounces =
                _path_tracing(context, secondary, traced, depth - 1);
            irradiance = attenuation * traced;
            return 1 + num_bounces;
        }
        else {
            irradiance = Eigen::Vector3f::Zero();
            return 1;
        }
    }
    else { // background
        auto raydir = get_raydir(rayhit).normalized();
        float t = 0.5f * (raydir(1) + 1.0f);
        irradiance = (1.0f - t) * Eigen::Vector3f::Ones() +
                     t * Eigen::Vector3f(0.5f, 0.7f, 1.0f);
        return 1;
    }
}

} // namespace yart
