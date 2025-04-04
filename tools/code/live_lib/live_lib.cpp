#include "cr/cr.h"
#include "ecs/ecs_scene.h"
#include "ecs/ecs_utilities.h"
#include "ecs/ecs_resources.h"
#include "ecs/ecs_cull.h"
#include "debug_render.h"

#include "imgui/imgui.h"

#define DLL 1
#include "ecs/ecs_live.h"
#include "str/Str.cpp"

#include "renderer.h"
#include "data_struct.h"
#include "timer.h"

#include "maths/maths.h"
#include "../../shader_structs/forward_render.h"

#include <stdio.h>
#include <set>
#include <vector>

#define JC_VORONOI_IMPLEMENTATION
#include "jc_voronoi/jc_voronoi.h"

using namespace put;
using namespace dbg;
using namespace ecs;

bool g_mesh = false;
bool g_debug = true;

namespace
{
    void draw_edges(const jcv_diagram* diagram);
    void draw_cells(const jcv_diagram* diagram);

    struct voronoi_map
    {
        jcv_diagram  diagram;
        u32          num_points;
        vec3f*       points = nullptr;
        jcv_point*   vpoints = nullptr;;
    };
    
    vec3f jcv_to_vec(const jcv_point& p)
    {
        // could transform in plane
        return vec3f(p.x, 0.0f, p.y);
    }
    
    voronoi_map* voronoi_map_generate()
    {
        voronoi_map* voronoi = new voronoi_map();
        
        // generates random points
        srand(0);
        voronoi->num_points = 30;
        for(u32 i = 0; i < voronoi->num_points; ++i)
        {
            // 3d point for rendering
            vec3f p = vec3f((rand() % 60) - 30, 0.0f, (rand() % 60) - 30);
            sb_push(voronoi->points, p);
            
            // 2d point for the diagram
            jcv_point vp;
            vp.x = p.x;
            vp.y = p.z;
            sb_push(voronoi->vpoints, vp);
        }
        
        memset(&voronoi->diagram, 0, sizeof(jcv_diagram));
        jcv_diagram_generate(voronoi->num_points, voronoi->vpoints, 0, 0, &voronoi->diagram);
        
        return voronoi;
    }
    
    void voronoi_map_draw_edges(const voronoi_map* voronoi)
    {
        const jcv_edge* edge = jcv_diagram_get_edges( &voronoi->diagram );
        u32 count = 0;
        u32 bb = 43;
        while( edge )
        {
            if(count < bb)
                add_line(vec3f(edge->pos[0].x, 0.0f, edge->pos[0].y), vec3f(edge->pos[1].x, 0.0f, edge->pos[1].y));
            
            edge = jcv_diagram_get_next_edge(edge);
            
            ++count;
        }
    }

    void voronoi_map_draw_points(const voronoi_map* voronoi)
    {
        for(u32 i = 0; i < voronoi->num_points; ++i)
        {
            add_point(voronoi->points[i], 1.0f, vec4f::yellow());
        }
    }

    void voronoi_map_draw_cells(const voronoi_map* voronoi)
    {
        const jcv_site* sites = jcv_diagram_get_sites( &voronoi->diagram );
        
        u32 s = 8;
        
        for( int i = 0; i < voronoi->diagram.numsites; ++i )
        {
            if(i != s)
                continue;
                
            const jcv_site* site = &sites[i];

            const jcv_graphedge* e = site->edges;
            
            u32 cc = 0;
            while( e )
            {
                vec3f p1 = jcv_to_vec(site->p);
                vec3f p2 = jcv_to_vec(e->pos[0]);
                vec3f p3 = jcv_to_vec(e->pos[1]);
                e = e->next;
                                
                if(cc < 5)
                    add_line(p2, p3);
                
                ++cc;
            }
        }
    }
}

struct live_lib
{
    u32                 box_start = 0;
    u32                 box_end;
    camera              cull_cam;
    ecs_scene*          scene;
    voronoi_map*        voronoi;
    
    void init(live_context* ctx)
    {
        scene = ctx->scene;
        
        ecs::clear_scene(scene);
        voronoi = voronoi_map_generate();
        
        // primitive resources
        material_resource* default_material = get_material_resource(PEN_HASH("default_material"));
        
        //test_mesh();
        //test_mesh2();
        //test_mesh3();
    
        const c8* primitive_names[] = {
            "subway",
            "lamp_post",
            "building"
        };
        
        geometry_resource** primitives = nullptr;
        
        for(u32 i = 0; i < PEN_ARRAY_SIZE(primitive_names); ++i)
        {
            sb_push(primitives, get_geometry_resource(PEN_HASH(primitive_names[i])));
        }
        
        // add lights
        u32 light = get_new_entity(scene);
        scene->names[light] = "front_light";
        scene->id_name[light] = PEN_HASH("front_light");
        scene->lights[light].colour = vec3f::one();
        scene->lights[light].direction = vec3f::one();
        scene->lights[light].type = e_light_type::dir;
        //scene->lights[light].flags = e_light_flags::shadow_map;
        scene->transforms[light].translation = vec3f::zero();
        scene->transforms[light].rotation = quat();
        scene->transforms[light].scale = vec3f::one();
        scene->entities[light] |= e_cmp::light;
        scene->entities[light] |= e_cmp::transform;
        
        light = get_new_entity(scene);
        scene->names[light] = "front_light";
        scene->id_name[light] = PEN_HASH("front_light");
        scene->lights[light].colour = vec3f::one();
        scene->lights[light].direction = vec3f(-1.0f, 0.0f, 1.0f);
        scene->lights[light].type = e_light_type::dir;
        scene->transforms[light].translation = vec3f::zero();
        scene->transforms[light].rotation = quat();
        scene->transforms[light].scale = vec3f::one();
        scene->entities[light] |= e_cmp::light;
        scene->entities[light] |= e_cmp::transform;
        
        // add primitve instances
        vec3f pos[] = {
            vec3f::unit_x() * - 6.0f,
            vec3f::unit_x() * - 3.0f,
            vec3f::unit_x() * 0.0f,
            vec3f::unit_x() * 3.0f,
            vec3f::unit_x() * 6.0f,
            
            vec3f::unit_x() * - 6.0f + vec3f::unit_z() * 3.0f,
            vec3f::unit_x() * - 3.0f + vec3f::unit_z() * 3.0f,
            vec3f::unit_x() * 0.0f + vec3f::unit_z() * 3.0f,
            vec3f::unit_x() * 3.0f + vec3f::unit_z() * 3.0f,
            vec3f::unit_x() * 6.0f + vec3f::unit_z() * 3.0f,
        };
        
        vec4f col[] = {
            vec4f::orange(),
            vec4f::yellow(),
            vec4f::green(),
            vec4f::cyan(),
            vec4f::magenta(),
            vec4f::white(),
            vec4f::red(),
            vec4f::blue(),
            vec4f::magenta(),
            vec4f::cyan()
        };
    }
    
    struct edge
    {
        vec3f start;
        vec3f end;
        mat4  mat;
    };
    edge** s_edge_strips = nullptr;
    
    void extrude(edge*& strip, vec3f dir)
    {
        struct edge ne;
        
        u32 i = sb_count(strip);
        
        ne.start = strip[i-1].start + dir;
        ne.end = strip[i-1].end + dir;
        ne.mat = mat4::create_identity();
        
        sb_push(strip, ne);
    }
    
    vec3f get_rot_origin(edge* strip, mat4 mr, vec3f rr)
    {
        mr = strip[0].mat;
        
        vec3f r = normalize(strip[0].end - strip[0].start);
        vec3f a = normalize(strip[0].start - strip[1].start);
        
        mr = mat::create_axis_swap(r, cross(a, r), a);
        mat4 inv = mat::inverse4x4(mr);
        
        vec3f min = FLT_MAX;
        vec3f max = -FLT_MAX;
        
        u32 ec = sb_count(strip);
        for(u32 i = 0; i < ec; ++i)
        {
            vec3f v = inv.transform_vector(strip[i].end);
            vec3f v2 = inv.transform_vector(strip[i].start);
            
            min = min_union(min, v);
            max = max_union(max, v);
            
            min = min_union(min, v2);
            max = max_union(max, v2);
        }
        
        vec3f corners[] = {
            min,
            {min.x, min.y, max.z},
            {min.x, max.y, max.z},
            {min.x, max.y, min.z},
            {max.x, min.y, min.z},
            {max.x, min.y, max.z},
            max,
            {max.x, max.y, min.z},
        };
        
        vec3f mid = mr.transform_vector(min + (max - min) * 0.5f);
        vec3f vdir = mid + rr;
        
        // find closest corner to the new direction
        vec3f cc;
        f32 cd = FLT_MAX;
        for(u32 i = 4; i < 8; ++i)
        {
            vec3f tc = mr.transform_vector(corners[i]);
            f32 d = dist(tc, vdir);
            if(d < cd)
            {
                cd = d;
                cc = tc;
            }
        }

        return cc;
    }
    
    void rotate_strip(edge*& strip, const mat4& rot)
    {
        u32 ec = sb_count(strip);
        for(u32 i = 0; i < ec; ++i)
        {
            auto& e = strip[i];
            e.start = rot.transform_vector(e.start);
            e.end = rot.transform_vector(e.end);
        }
    }
    
    edge* bend(edge**& strips, f32 length, vec2f v)
    {
        edge* strip = strips[sb_count(strips)-1];
        
        edge* ee = nullptr;
        edge* ej = nullptr;
                        
        vec3f right = normalize(strip[0].end - strip[0].start);
        vec3f va = normalize(strip[1].start - strip[0].start);
        vec3f vu = cross(va, right);
        
        mat4 mb = mat::create_rotation(vu, v.y);
        mb *= mat::create_rotation(va, v.x);
                
        vec3f new_right = mb.transform_vector(right) * sgn(length);
                
        vec3f rot_origin = get_rot_origin(strip, mb, new_right);
        
        mat4 mr;
        mr = mat::create_translation(rot_origin);
        mr *= mb;
        mr *= mat::create_translation(-rot_origin);
        
        vec3f* prev_pos = nullptr;
        
        u32 ec = sb_count(strip);
        for(u32 i = 0; i < ec; ++i)
        {
            auto e = strip[i];
            
            e.start = e.end;
            e.end += right * length;
            e.mat = mb;
            
            e.start = mr.transform_vector(e.start);
            e.end = mr.transform_vector(e.end);
            
            sb_push(ee, e);
            sb_push(prev_pos, strip[i].end);
        }
        
        u32 res = 16;
        for(u32 j = 1; j < res+1; ++j)
        {
            u32 ec = sb_count(strip);
            for(u32 i = 0; i < ec; ++i)
            {
                mat4 mj;
                mj = mat::create_translation(rot_origin);
                
                mat4 mb2 = mat::create_rotation(vu, (v.y/(f32)res) * j);
                mb2 *= mat::create_rotation(va, (v.x/(f32)res) * j);
                
                mj *= mb2;
                mj *= mat::create_translation(-rot_origin);
                                
                vec3f ip = mj.transform_vector(strip[i].end);
                
                auto e = strip[i];
                e.start = prev_pos[i];
                e.end = ip;
                sb_push(ej, e);
                
                prev_pos[i] = ip;
            }
            
            sb_push(strips, ej);
            ej = nullptr;
        }
    
        sb_push(strips, ee);
        return ej;
    }
    
    int on_load(live_context* ctx)
    {
        init(ctx);
        generate();
                
        return 0;
    }
    
    void mesh_from_strips(Str name, edge** strips, bool flip = false)
    {
        vertex_model* verts = nullptr;
        
        u32 num_strips = sb_count(strips);
        for(u32 s = 0; s < num_strips; ++s)
        {
            u32 ec = sb_count(strips[s]);
            for(u32 i = 0; i < ec; ++i)
            {
                auto& e1 = strips[s][i];
                
                if(i < ec-1)
                {
                    auto& e2 = strips[s][i+1];
                                        
                    vec3f vv[] = {
                        e1.end,
                        e1.start,
                        e2.start,
                        e2.start,
                        e2.end,
                        e1.end
                    };
                                        
                    vec3f t = normalize(e1.end - e1.start);
                    if(mag2(e1.end - e1.start) < 0.0001)
                    {
                        t = normalize(e2.end - e2.start);
                    }
                    
                    vec3f b = normalize(e2.start - e1.start);
                    if(mag2(e2.start - e1.start) < 0.0001)
                    {
                        b = normalize(e2.end - e1.end);
                    }

                    vec3f n = cross(t, b);
                    
                    if(!flip)
                    {
                        std::swap(vv[0], vv[1]);
                        std::swap(vv[3], vv[4]);
                        
                        n *= -1.0f;
                        t *= -1.0f;
                        b *= -1.0f;
                    }
                    
                    for(u32 k = 0; k < 6; ++k)
                    {
                        vertex_model v;
                        v.pos = vec4f(vv[k], 1.0f);
                        v.normal = vec4f(n, 0.0f);
                        v.tangent = vec4f(t, 0.0f);
                        v.bitangent = vec4f(b, 0.0f);
                    
                        sb_push(verts, v);
                    }
                }
            }
        }
        
        // cap.. will become buildings
        vec3f* inner_loop = nullptr;
        for(u32 s = 0; s < num_strips; ++s)
        {
            sb_push(inner_loop, strips[s][0].end);
        }
        
        u32 nl = sb_count(inner_loop);
        vec3f* cap_hull = nullptr;
        convex_hull_from_points(cap_hull, inner_loop, nl);
        
        u32 cl = sb_count(cap_hull);
        vec3f mid = get_convex_hull_centre(cap_hull, cl);

        f32 area = convex_hull_area(cap_hull, sb_count(cap_hull));
        f32 h = area < 0.75f ? 0.0f : area * ((rand() % 255) / 255.0f) * 0.5f + 0.2f;

        h = 0.1f;

        for(u32 s = 0; s < cl; ++s)
        {
            u32 next = (s + 1) % cl;

            vec3f offset = vec3f(0.0f, h, 0.0f);
            
            // one tri per hull side
            vec3f vv[3] = {
                cap_hull[s] + offset,
                mid + offset,
                cap_hull[next] + offset
            };
            
            vec3f t = vec3f::unit_x();
            vec3f b = vec3f::unit_z();
            vec3f n = vec3f::unit_y();
            
            for(u32 k = 0; k < 3; ++k)
            {
                vertex_model v;
                v.pos = vec4f(vv[k], 1.0f);
                v.normal = vec4f(n, 0.0f);
                v.tangent = vec4f(t, 0.0f);
                v.bitangent = vec4f(b, 0.0f);
            
                sb_push(verts, v);
            }

            // quad to join the loop
            vec3f vq[6] = {
                cap_hull[s] + offset,
                cap_hull[next] + offset,
                cap_hull[next],

                cap_hull[s] + offset,
                cap_hull[next],
                cap_hull[s]
            };

            vec3f qt = normalize(vq[1] - vq[0]);
            vec3f qb = normalize(vq[1] - vq[2]);
            vec3f qn = cross(qt, qb); //normalize(maths::get_normal(vq[0], vq[1], vq[2]));

            for(u32 k = 0; k < 6; ++k)
            {
                vertex_model v;
                v.pos = vec4f(vq[k], 1.0f);

                v.normal = vec4f(qn, 0.0f);
                v.tangent = vec4f(qt, 0.0f);
                v.bitangent = vec4f(qb, 0.0f);
            
                sb_push(verts, v);
            }
        }
        
        create_primitive_resource_faceted(name, verts, sb_count(verts));
    }
    
    void test_mesh()
    {
        if(s_edge_strips)
        {
            sb_free(s_edge_strips);
            s_edge_strips = nullptr;
        }
        
        sb_push(s_edge_strips, nullptr);
        
        edge e;
        e.start = vec3f(0.5f);
        e.end = vec3f(0.5f) + vec3f::unit_x() * 5.0f;
        e.mat = mat4::create_identity();
        sb_push(s_edge_strips[0], e);

        extrude(s_edge_strips[0], vec3f::unit_z() * -10.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * 10.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -10.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * 10.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * 1.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * 1.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -2.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * -2.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -5.0f);
        
        // ...
        
        static const f32 theta = -M_PI/8.0;
        
        bend(s_edge_strips, 10.0f, vec2f(theta, 0.0));
        bend(s_edge_strips, 10.0f, vec2f(-theta, 0.0));
        bend(s_edge_strips, 10.0f, vec2f(0.0f, theta*4.0f));
        bend(s_edge_strips, 10.0f, vec2f(0.0f, theta*4.0f));
        bend(s_edge_strips, 30.0f, vec2f(theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(-theta, 0.0f));
        
        bend(s_edge_strips, 30.0f, vec2f(0.0f, theta));
        bend(s_edge_strips, 30.0f, vec2f(-theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(-theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(theta*2.0f, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(0.0f, theta*4.0f));
        
        bend(s_edge_strips, 30.0f, vec2f(theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(theta, 0.0f));
        bend(s_edge_strips, 30.0f, vec2f(theta, 0.0f));
        
        bend(s_edge_strips, 30.0f, vec2f(0.0, theta));
        bend(s_edge_strips, 30.0f, vec2f(0.0, theta));
        bend(s_edge_strips, 30.0f, vec2f(0.0, theta));
        bend(s_edge_strips, 30.0f, vec2f(0.0, theta));
        
        for(u32 j = 0; j < 64; ++j)
        {
            bend(s_edge_strips, 5.0f, vec2f(theta));
        }
        
        mesh_from_strips("subway", s_edge_strips, true);
    }
    
    void test_mesh2()
    {
        if(s_edge_strips)
        {
            sb_free(s_edge_strips);
            s_edge_strips = nullptr;
        }
        
        sb_push(s_edge_strips, nullptr);
        
        edge e;
        e.start = vec3f(0.5f);
        e.end = vec3f(0.5f) + vec3f::unit_y();
        e.mat = mat4::create_identity();
        sb_push(s_edge_strips[0], e);

        extrude(s_edge_strips[0], vec3f::unit_x() * 0.5f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -0.5f);
        extrude(s_edge_strips[0], vec3f::unit_x() * -0.5f);
        extrude(s_edge_strips[0], vec3f::unit_z() * 0.5f);
        
        static const f32 theta = -M_PI/8.0;
        
        bend(s_edge_strips, 20.0f, vec2f(0.0, 0.0));
        bend(s_edge_strips, 10.0f, vec2f(-theta*4.0f, 0.0));
        
        mesh_from_strips("lamp_post", s_edge_strips);
    }
    
    void test_mesh3()
    {
        if(s_edge_strips)
        {
            sb_free(s_edge_strips);
            s_edge_strips = nullptr;
        }
        
        sb_push(s_edge_strips, nullptr);
        
        edge e;
        e.start = vec3f(0.0f, 20.0f, -25.0f);
        e.end = e.start - vec3f::unit_x() * 20.0f;
        e.mat = mat4::create_identity();
        sb_push(s_edge_strips[0], e);

        extrude(s_edge_strips[0], vec3f::unit_y() * 5.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -2.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * 20.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * 2.0f);
        extrude(s_edge_strips[0], vec3f::unit_y() * 2.0f);
        extrude(s_edge_strips[0], vec3f::unit_z() * -2.0f);
        
        static const f32 theta = -M_PI/2.0;
        
        bend(s_edge_strips, 20.0f, vec2f(-theta, 0.0));
        bend(s_edge_strips, 20.0f, vec2f(-theta, 0.0));
        bend(s_edge_strips, 20.0f, vec2f(-theta, 0.0));
        bend(s_edge_strips, 20.0f, vec2f(-theta, 0.0));
        
        mesh_from_strips("building", s_edge_strips);
    }
    
    void draw_edge_strip_triangles(edge** edge_strips)
    {
        u32 num_strips = sb_count(edge_strips);
        for(u32 s = 0; s < num_strips; ++s)
        {
            u32 ec = sb_count(edge_strips[s]);
            for(u32 i = 0; i < ec; ++i)
            {
                auto& e1 = edge_strips[s][i];
                
                if(i < ec-2)
                {
                    auto& e2 = edge_strips[s][i+1];
                    
                    //add_triangle(e1.start, e1.end, e2.start);
                    //add_triangle(e2.start, e2.end, e1.end);
                }
                else
                {
                    if(i < ec-1)
                        add_line(e1.start, e1.end, vec4f::red());
                }
            }
        }
        
        // cap
        /*
        vec3f* inner_loop = nullptr;
        for(u32 s = 0; s < num_strips; ++s)
        {
            sb_push(inner_loop, edge_strips[s][0].end);
        }
        
        u32 nl = sb_count(inner_loop);
        vec3f* cap_hull = nullptr;
        convex_hull_from_points(cap_hull, inner_loop, nl);
        
        u32 cl = sb_count(cap_hull);
        for(u32 s = 0; s < cl; ++s)
        {
            u32 t = (s + 1) % cl;
            add_line(cap_hull[s], cap_hull[t], vec4f::magenta());
        }
        */
    }
    
    void voronoi_map_draw_cells(const voronoi_map* voronoi)
    {
        // If you want to draw triangles, or relax the diagram,
        // you can iterate over the sites and get all edges easily
        const jcv_site* sites = jcv_diagram_get_sites( &voronoi->diagram );
        
        u32 s = 8;
        
        for( int i = 0; i < voronoi->diagram.numsites; ++i )
        {
            if(i != s)
                continue;
                
            const jcv_site* site = &sites[i];

            const jcv_graphedge* e = site->edges;
            
            u32 cc = 0;
            while( e )
            {
                vec3f p1 = jcv_to_vec(site->p);
                vec3f p2 = jcv_to_vec(e->pos[0]);
                vec3f p3 = jcv_to_vec(e->pos[1]);
                e = e->next;
                
                //add_triangle(p1, p2, p3);
                
                if(cc < 5)
                    add_line(p2, p3);
                //break;
                
                ++cc;
            }
        }
    }
    
    void subdivide_hull(const vec3f& start_pos, const vec3f& axis, const vec3f& right, vec3f* inset_edge_points)
    {
        u32 nep = sb_count(inset_edge_points);
        u32 sub = 50;
        f32 spacing = 5.0f;
        for(u32 s = 0; s < sub; ++s)
        {
            vec3f p = start_pos + right * (f32)s * spacing;
            vec3f ray = p - axis * 10000.0f;
            p += axis * 10000.0f;
            
            vec3f* ips = nullptr;
            for(s32 i = 0; i < nep; i++)
            {
                s32 n = (i+1)%nep;
                
                vec3f p2 = inset_edge_points[i];
                vec3f p3 = inset_edge_points[n];
                
                vec3f ip;
                if(maths::line_vs_line(p, ray, p2, p3, ip))
                {
                    sb_push(ips, ip);
                }
            }
            
            if(sb_count(ips) == 2)
                add_line(ips[0], ips[1], vec4f::magenta());
        }
    }
    
    vec3f get_convex_hull_centre(vec3f* hull, u32 count)
    {
        vec3f mid = vec3f::zero();
        for(u32 i = 0; i < count; ++i)
        {
            mid += hull[i];
        }
        
        mid /= (f32)count;
        return mid;
    }
    
    bool point_inside_convex_hull(vec3f* hull, size_t ncp, const vec3f& up, const vec3f& p0)
    {
        for(size_t i = 0; i < ncp; ++i)
        {
            size_t i2 = (i+1)%ncp;
            
            vec3f p1 = hull[i];
            vec3f p2 = hull[i2];
            
            vec3f v1 = p2 - p1;
            vec3f v2 = p0 - p1;
            
            if(dot(cross(v2,v1), up) > 0.0f)
                return false;
        }
        
        return true;
    }

    vec3f closest_point_on_convex_hull(vec3f* hull, size_t ncp, vec3f p)
    {
        f32 cd = FLT_MAX;
        vec3f cp = vec3f::zero();
        for(u32 i = 0; i < ncp; ++i)
        {
            u32 n = (i+1)%ncp;
            vec3f lp = maths::closest_point_on_line(hull[i], hull[n], p);
            f32 d = mag2(p-lp);
            if(d < cd)
            {
                cp = lp;
                cd = d;
            }
        }
        return cp;
    }

    vec3f closest_point_of_convex_hull(vec3f* hull, size_t ncp, vec3f p)
    {
        f32 cd = FLT_MAX;
        vec3f cp = vec3f::zero();
        for(u32 i = 0; i < ncp; ++i)
        {
            f32 d = mag2(hull[i]-p);
            if(d < cd)
            {
                cp = hull[i];
                cd = d;
            }
        }
        return cp;
    }

    f32 convex_hull_area(vec3f* points, size_t num_points)
    {
        f32 sum = 0.0f;
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i + 1) % num_points;
            vec3f v1 = points[i];
            vec3f v2 = points[n];

            sum += v1.x * v2.z - v2.x * v1.z;
        }

        return 0.5f * abs(sum);
    }
    
    void convex_hull_from_points(vec3f*& hull, vec3f* points, size_t num_points)
    {
        vec3f* to_sort = nullptr;
        bool*  visited = nullptr;
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 count = sb_count(to_sort);
            bool dupe = false;
            for (u32 j = 0; j < count; ++j)
            {
                if(almost_equal(to_sort[j], points[i], 0.001f))
                    dupe = true;
            }
            
            if (!dupe)
            {
                sb_push(to_sort, points[i]);
                sb_push(visited, false);
            }
        }
        num_points = sb_count(to_sort);

        //find right most
        num_points = sb_count(to_sort);
        vec3f cur = to_sort[0];
        size_t curi = 0;
        for (size_t i = 1; i < num_points; ++i)
        {
            if(to_sort[i].z >= cur.z)
                //if (to_sort[i].x >= cur.x)
                {
                    cur = to_sort[i];
                    curi = i;
                }
        }
        
        // wind
        sb_push(hull, cur);
        u32 iters = 0;
        for(;;)
        {
            size_t rm = (curi+1)%num_points;
            vec3f x1 = to_sort[rm];

            for (size_t i = 0; i < num_points; ++i)
            {
                if(i == curi)
                    continue;

                if (visited[i])
                    continue;
                
                vec3f x2 = to_sort[i];
                vec3f v1 = x1 - cur;
                vec3f v2 = x2 - cur;

                vec3f cp = cross(v2, v1);
                if (cp.y > 0.0f)
                {
                    x1 = to_sort[i];
                    rm = i;
                }
            }

            f32 diff = mag2(x1 - hull[0]);
            if(almost_equal(x1, hull[0], 0.01f))
                break;
            
            cur = x1;
            curi = rm;
            visited[rm] = true;
            sb_push(hull, x1);
            ++iters;

            // saftey break, but we shouldnt hit this
            if (iters > num_points)
                break;
        }
        
        sb_free(to_sort);
    }

    bool line_vs_convex_hull(vec3f l1, vec3f l2, vec3f* hull, u32 num_points, vec3f& ip)
    {
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i + 1) % num_points;

            l1.y = 0.0f;
            l2.y = 0.0f;
            hull[i].y = 0.0f;
            hull[n].y = 0.0f;

            if(maths::line_vs_line(hull[i], hull[n], l1, l2, ip))
            {
                return true;
            }
        }

        return false;
    }

    bool line_vs_convex_hull_ex(vec3f l1, vec3f l2, vec3f* hull, u32 num_points, vec3f& ip)
    {
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i + 1) % num_points;

            l1.y = 0.0f;
            l2.y = 0.0f;
            hull[i].y = 0.0f;
            hull[n].y = 0.0f;

            if(maths::line_vs_line(hull[i], hull[n], l1, l2, ip))
            {
                f32 d = abs(dot(normalize(hull[n] - hull[i]), normalize(l2 - l1)));
                return d < 0.01f;
            }
        }

        return false;
    }

    bool line_vs_convex_hull_ex2(vec3f l1, vec3f l2, vec3f* hull, u32 num_points, vec3f& ip, vec3f& perp)
    {
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i + 1) % num_points;

            l1.y = 0.0f;
            l2.y = 0.0f;
            hull[i].y = 0.0f;
            hull[n].y = 0.0f;

            if(maths::line_vs_line(hull[i], hull[n], l1, l2, ip))
            {
                perp = cross(normalize(hull[n] - hull[i]), vec3f::unit_y());
                return true;
            }
        }

        return false;
    }

    void draw_convex_hull(const vec3f* points, size_t num_points, vec4f col = vec4f::white())
    {
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i + 1) % num_points;
            add_line(points[i], points[n], col);
            //add_point(points[i], 0.1f, col/(f32)i);
        }
    }

    f32 convex_hull_distance(const vec3f* hull, u32 num_points, const vec3f& p)
    {
        f32 cd = FLT_MAX;
        for (u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i+1) % num_points;
            f32 d = maths::point_segment_distance(hull[i], hull[n], p);
            cd = std::min<f32>(d, cd);
        }
        return cd;
    }

    vec3f* convex_hull_tidy(vec3f* hull, u32 num_points, f32 weld_distance)
    {
        // weld points that are less than weld_distance apart
        for(s32 i = 0; i < num_points; i++)
        {
            for(s32 j = i + 1; j < num_points; j++)
            {
                vec3f v = hull[i] - hull[j];
                if(mag2(v))
                {
                    if(mag(v) < weld_distance)
                    {
                        hull[j] = hull[i];
                    }
                }
            }
        }
        
        // strip out dead edges
        vec3f* tidy = nullptr;
        for(s32 i = 0; i < num_points; i++)
        {
            s32 n = (i+1)%num_points;
            
            vec3f p2 = hull[i];
            vec3f p3 = hull[n];
            
            if(mag2(p3-p2))
            {
                sb_push(tidy, p2);
            }
        }

        sb_free(hull);
        return tidy;
    }
    
    void curb(edge**& edge_strips, vec3f* hull_points)
    {
        vec3f right = normalize(hull_points[1] - hull_points[0]);
        vec3f up = vec3f::unit_y();
        vec3f at = normalize(cross(right, up));
        
        f32 height = ((f32)(rand()%RAND_MAX) / (f32)RAND_MAX) * 5.0f;
        height = 0.1f;
        vec3f ystart = vec3f(0.0f, height, 0.0f);
                
        edge e;
        e.start = hull_points[0] + ystart;
        e.end = hull_points[1] + ystart;
        e.mat = mat4::create_identity();
        
        f32 scale = 0.1f;
        
        sb_push(edge_strips[0], e);
        extrude(edge_strips[0], at * scale * 2.0f);
        extrude(edge_strips[0], vec3f::unit_y() * -scale * 10.0f * height);
                
        vec3f prev_vl = right;
                
        u32 nep = sb_count(hull_points);
        
        vec3f bl = vec3f::flt_max();
        for(s32 i = 0; i < nep+1; i++)
        {
            s32 n = (i+1)%nep;
            s32 ii = (i)%nep;
            
            vec3f p2 = hull_points[ii];
            vec3f p3 = hull_points[n];
     
            bl.x = std::min<f32>(p2.x, bl.x);
            bl.z = std::min<f32>(p2.z, bl.z);
            bl.y = std::min<f32>(p2.y, bl.y);
            
            if(i == 0)
                continue;
            
            vec3f vl = normalize(vec3f(p3-p2));
            
            f32 yaw = acos(dot(vl, prev_vl));
            if (std::isnan(yaw))
            {
                yaw = 0.0f;
            }
      
            bend(edge_strips, mag(p3-p2), vec2f(0.0, yaw));
                        
            prev_vl = vl;
        }
    }

    struct segment
    {
        vec3f p1, p2;
    };

    struct road_network_params
    {
        f32 inset = 0.5f;
        f32 major_inset = 0.75f;
        f32 subdiv_size = 0.4f;
        f32 pavement_width = 0.2f;
    };

    struct road_section
    {
        segment          entry;
        segment          exit;
        segment          connector;
        segment          entry_connector;
        vec3f            dir;
        std::vector<u32> destinations;
        bool             outer;
    };

    struct junction
    {
        std::set<u32>       exits;
        std::set<u32>       entries;
        std::vector<vec3f>  corner_points;
    };

    struct outer_join
    {
        vec3f v[3];
    };

    typedef std::vector<vec3f> convex_hull;

    struct road_network
    {
        road_network_params             params = {};
        vec3f*                          outer_hull = nullptr;
        vec3f*                          inset_hull = nullptr;
        vec3f*                          inner_road_hull = nullptr;
        segment                         extent_axis_points[2] = {};
        f32                             extent_axis_length[2] = {};
        vec3f                           extent_axis[2] = {};
        vec3f                           up = vec3f::zero();
        vec3f                           right = vec3f::zero();
        vec3f                           at = vec3f::zero();
        vec3f**                         valid_sub_hulls = nullptr;
        std::vector<road_section>       road_sections;
        std::vector<junction>           junctions;
        std::vector<outer_join>         joins;
    };

    void get_hull_subdivision_extents(road_network& net)
    {
        // basis for hull
        net.right = normalize(net.inset_hull[1] - net.inset_hull[0]);
        net.up = vec3f::unit_y();
        net.at = normalize(cross(net.right, net.up));
                
        edge e;
        e.start = net.inset_hull[0];
        e.end = net.inset_hull[1];
        e.mat = mat4::create_identity();
                
        // get extents in right axis
        vec3f side_start = e.start - net.right * 1000.0f;
        vec3f side_end = e.start + net.right * 1000.0f;
        vec3f side_v = normalize(side_end - side_start);

        f32 mind = FLT_MAX;
        f32 maxd = -FLT_MAX;

        vec3f mincp;
        vec3f maxcp;
        
        u32 num_points = sb_count(net.inset_hull);
        for(s32 i = 0; i < num_points; i++)
        {
            f32 d  = maths::distance_on_line(side_start, side_end, net.inset_hull[i]);
            if(d > maxd)
            {
                maxd = d;
                maxcp = maths::closest_point_on_ray(side_start, side_v, net.inset_hull[i]);
            }
            
            if(d < mind)
            {
                mind = d;
                mincp = maths::closest_point_on_ray(side_start, side_v, net.inset_hull[i]);
            }
        }
        net.extent_axis_points[0].p1 = mincp;
        net.extent_axis_points[0].p2 = maxcp;

        // get extents in at axis
        vec3f side_perp_start = mincp - net.at * 1000.0f;
        vec3f side_perp_end = mincp + net.at * 1000.0f;
        vec3f side_perp_v = normalize(side_perp_end - side_perp_start);
        
        net.extent_axis_points[1].p1 = mincp;
        maxd = -FLT_MAX;
        mind = FLT_MAX;
        for(s32 i = 0; i < num_points; i++)
        {
            f32 d  = maths::distance_on_line(side_perp_start, side_perp_end, net.inset_hull[i]);
            if(d < mind)
            {
                mind = d;
                mincp = maths::closest_point_on_ray(side_perp_start, side_perp_v, net.inset_hull[i]);
            }
        }
        
        net.extent_axis_points[1].p2 = mincp;

        net.extent_axis_length[0] = mag(net.extent_axis_points[0].p2 - net.extent_axis_points[0].p1);
        net.extent_axis_length[1] = mag(net.extent_axis_points[1].p2 - net.extent_axis_points[1].p1);

        net.extent_axis[0] = normalize(net.extent_axis_points[0].p2 - net.extent_axis_points[0].p1);
        net.extent_axis[1] = normalize(net.extent_axis_points[1].p2 - net.extent_axis_points[1].p1);
    }

    void subdivide_as_grid(road_network& net)
    {
        u32 x =  net.extent_axis_length[0] / net.params.subdiv_size;
        u32 y =  net.extent_axis_length[1] / net.params.subdiv_size;
        
        f32 half_size = (net.params.subdiv_size * 0.5f) - net.params.inset;
        f32 size = half_size + net.params.inset;

        bool valid = false;
        u32 count = 0;

        vec3f** valid_sub_hulls = nullptr;
        vec3f** invalid_sub_hulls = nullptr;

        vec2i* valid_grid_index = nullptr;
        vec2i* invalid_grid_index = nullptr;

        vec3f* junction_pos = nullptr;

        u32 num_points = sb_count(net.inset_hull);
        
        for(u32 i = 0; i < x+1; ++i)
        {
            for(u32 j = 0; j < y+1; ++j)
            {
                f32 xt = (f32)i * net.params.subdiv_size;
                f32 yt = (f32)j * net.params.subdiv_size;
                
                vec3f py = net.extent_axis_points[1].p1 + net.extent_axis[1] * (yt + half_size);
                vec3f px = py + net.extent_axis[0] * (xt + half_size);
                
                vec3f corners[4] = {
                    px - net.extent_axis[0] * half_size - net.extent_axis[1] * half_size,
                    px + net.extent_axis[0] * half_size - net.extent_axis[1] * half_size,
                    px + net.extent_axis[0] * half_size + net.extent_axis[1] * half_size,
                    px - net.extent_axis[0] * half_size + net.extent_axis[1] * half_size,
                };

                vec3f junctions[4] = {
                    px - net.extent_axis[0] * size - net.extent_axis[1] * size,
                    px + net.extent_axis[0] * size - net.extent_axis[1] * size,
                    px + net.extent_axis[0] * size + net.extent_axis[1] * size,
                    px - net.extent_axis[0] * size + net.extent_axis[1] * size,
                };

                for (u32 jj = 0; jj < 4; ++jj)
                {
                    if (point_inside_convex_hull(net.inset_hull, num_points, net.up, junctions[jj]))
                    {
                        sb_push(junction_pos, junctions[jj]);
                    }
                }
                
                vec3f* sub_hull_points = nullptr;
                for(u32 c = 0; c < 4; ++c)
                {
                    u32 d = (c + 1) % 4;
                    
                    bool inside = false;

                    if(point_inside_convex_hull(net.inset_hull, num_points, net.up, corners[c]))
                    {
                        valid = true;
                        inside = true;
                        sb_push(sub_hull_points, corners[c]);
                    }
                    else if(point_inside_convex_hull(net.inset_hull, num_points, net.up, corners[d]))
                    {
                        valid = true;
                        inside = true;
                        sb_push(sub_hull_points, corners[d]);
                    }
                    
                    // intersect with hull
                    for(s32 i = 0; i < num_points; i++)
                    {
                        s32 n = (i + 1) % num_points;
                        
                        vec3f p0 = net.inset_hull[i];
                        vec3f p1 = net.inset_hull[n];

                        vec3f ip;
                        if(maths::line_vs_line(p0, p1, corners[c], corners[d], ip))
                        {
                            sb_push(sub_hull_points, ip);
                        }
                        
                        if(point_inside_convex_hull(corners, 4, net.up, p0))
                        {
                            sb_push(sub_hull_points, p0);
                        }
                    }
                }
                
                u32 sp = sb_count(sub_hull_points);
                if(sp > 0)
                {                    
                    vec3f* sub_hull = nullptr;
                    convex_hull_from_points(sub_hull, sub_hull_points, sp);

                    // must be at least a tri
                    if(sb_count(sub_hull) < 3)
                        continue;

                    f32 area = convex_hull_area(sub_hull, sb_count(sub_hull));
                    //if (area < net.params.inset)
                    if(0)
                    {
                        sb_push(invalid_sub_hulls, sub_hull);
                        sb_push(invalid_grid_index, vec2i(i, j));
                        continue;
                    }
                    else
                    {
                        sb_push(valid_sub_hulls, sub_hull);
                        sb_push(valid_grid_index, vec2i(i, j));
                    }
                    count++;
                }
            }
        }

        u32 num_invalid = sb_count(invalid_sub_hulls);
        u32 num_valid = sb_count(valid_sub_hulls);

        // join small hulls to neighbours
        /*
        for (u32 i = 0; i < num_invalid; ++i)
        {
            vec3f vc = get_convex_hull_centre(invalid_sub_hulls[i], sb_count(invalid_sub_hulls[i]));
            //draw_convex_hull(invalid_sub_hulls[i], sb_count(invalid_sub_hulls[i]), vec4f::red());

            vec2i igx = invalid_grid_index[i];

            // find closest hull to join with
            f32 cd = FLT_MAX;
            u32 cj = -1;
            for (u32 j = 0; j < num_valid; ++j)
            {
                vec2i vgx = valid_grid_index[j];

                if (abs(igx.x - vgx.x) + abs(igx.y - vgx.y) > 1)
                    continue;

                vec3f ic = get_convex_hull_centre(valid_sub_hulls[j], sb_count(valid_sub_hulls[j]));
                f32 d = mag2(vc - ic);

                if (d < cd)
                {
                    cd = d;
                    cj = j;
                }
            }

            if (cj != -1)
                for (u32 p = 0; p < sb_count(invalid_sub_hulls[i]); ++p)
                    sb_push(valid_sub_hulls[cj], invalid_sub_hulls[i][p]);
        }
        */

        // 
        vec3f** output_sub_hulls = nullptr;
        for(u32 i = 0; i < num_valid; ++i)
        {
            vec3f* combined_hull = nullptr;
            convex_hull_from_points(combined_hull, valid_sub_hulls[i], sb_count(valid_sub_hulls[i]));
            sb_push(output_sub_hulls, combined_hull);
        }

        net.valid_sub_hulls = output_sub_hulls;
    }

    void add_section_to_junction(junction& junc, road_section& sec, u32 section_index)
    {
        junc.exits.insert(section_index);

        for(auto& d : sec.destinations)
        {
            junc.entries.insert(d);
        }
    }

    void add_section_to_junctions(std::vector<junction>& junctions, std::vector<road_section>& sections, u32 section)
    {
        std::vector<junction*> intersect_junctions;

        for(auto& existing : junctions)
        {
            // check for intersections with other exits
            for(auto& exit : existing.exits)
            {
                if(exit != section)
                {
                    auto& ce = sections[exit].connector;
                    auto& cs = sections[section].connector;

                    vec3f ip;
                    bool intersects = maths::line_vs_line(ce.p1, ce.p2, cs.p1, cs.p2, ip);
                    if(intersects)
                    {
                        intersect_junctions.push_back(&existing);
                        break;
                    }
                }
            }

            // check for intersection with other entries
            for(auto& entry : existing.entries)
            {
                if(entry != section)
                {
                    auto& ce = sections[entry].entry_connector;
                    auto& cs = sections[section].connector;

                    vec3f ip;
                    bool intersects = maths::line_vs_line(ce.p1, ce.p2, cs.p1, cs.p2, ip);
                    if(intersects)
                    {
                        intersect_junctions.push_back(&existing);
                        break;
                    }
                }
            }
        }

        if(intersect_junctions.size() == 1)
        {
            add_section_to_junction(*intersect_junctions[0], sections[section], section);
        }
        else if(intersect_junctions.size() > 1)
        {
            // add to the first
            add_section_to_junction(*intersect_junctions[0], sections[section], section);

            for(auto& combine : intersect_junctions)
            {
                if(combine != intersect_junctions[0])
                {
                    for(auto& entry : combine->entries)
                    {
                        intersect_junctions[0]->entries.insert(entry);
                    }
                    //combine->entries.clear();

                    for(auto& exit : combine->exits)
                    {
                        intersect_junctions[0]->exits.insert(exit);
                    }
                    //combine->exits.clear();
                }
            }
        }
        else
        {
            // add a new junction
            junction new_junction;
            add_section_to_junction(new_junction, sections[section], section);
            junctions.push_back(new_junction);
        }
    }

    void extend_outer_road(road_network& net)
    {
        // find all points on the outsise
        vec3f* points = nullptr;
        for(auto& section : net.road_sections)
        {
            sb_push(points, section.exit.p2);
            sb_push(points, section.entry.p2);
        }

        net.inner_road_hull = nullptr;
        convex_hull_from_points(net.inner_road_hull, points, sb_count(points));

        std::vector<vec3f> projected;
        u32 num_points = sb_count(net.inner_road_hull);
        for(u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i+1)%num_points;

            vec3f cp = closest_point_on_convex_hull(net.outer_hull, sb_count(net.outer_hull), net.inner_road_hull[i]);
            vec3f cpn = closest_point_on_convex_hull(net.outer_hull, sb_count(net.outer_hull), net.inner_road_hull[n]);
            
            projected.push_back(cp);
            projected.push_back(cpn);
        }

        num_points = sb_count(net.outer_hull);
        for(u32 i = 0; i < num_points; ++i)
        {
            sort(projected.begin(), projected.end(), 
                [i, net](const vec3f & a, const vec3f & b) -> bool
            { 
                return dist2(a, net.outer_hull[i]) < dist2(b, net.outer_hull[i]);
            });

            net.joins.push_back({projected[0], net.outer_hull[i], net.outer_hull[i]});
        }
    }

    void generate_roads_and_junctions(road_network& net)
    {
        // intersection point result
        vec3f ip;

        // find road sections
        u32 num_hulls = sb_count(net.valid_sub_hulls);
        for(u32 h = 0; h < num_hulls; ++h)
        {
            u32 num_points = sb_count(net.valid_sub_hulls[h]);
            for(u32 i = 0; i < num_points; ++i)
            {
                // get perp of edge
                auto& loop = net.valid_sub_hulls[h];
                u32 n = (i+1)%num_points;
                vec3f ve = normalize(loop[n] - loop[i]);
                vec3f perp = cross(ve, vec3f::unit_y());
                f32 pw = net.params.pavement_width;

                // intersect perps with the outer hull
                vec3f junction_perps[] = {
                    loop[i] + perp * pw, loop[i] + perp * 4.0f,
                    loop[n] + perp * pw, loop[n] + perp * 4.0f,
                };

                vec3f ips[2] = {
                    junction_perps[1],
                    junction_perps[3]
                };

                road_section section;
                section.entry.p1 = junction_perps[0];
                section.entry.p2 = loop[i] + perp * net.params.inset;
                section.exit.p1 = junction_perps[2];
                section.exit.p2 = loop[n] + perp * net.params.inset;
                section.dir = ve;
                section.outer = false;
                net.road_sections.push_back(section);
            }
        }

        // set of road section indices which other roads have connected into
        std::set<u32> enters;

        auto& road_sections = net.road_sections;

        // connect road sections
        u32 num_sections = road_sections.size();
        for(u32 i = 0; i < num_sections; ++i)
        {
            vec3f vray = road_sections[i].dir;
            vec3f ray_pos = road_sections[i].exit.p1 + (road_sections[i].exit.p2 - road_sections[i].exit.p1) * 0.5f;

            f32 cd = FLT_MAX;
            s32 cj = -1;
            vec3f cip = vec3f::zero();

            for(u32 j = 0; j < num_sections; ++j)
            {
                if(i != j)
                {
                    if(maths::line_vs_line(ray_pos, ray_pos + vray * 100.0f, road_sections[j].entry.p1, road_sections[j].entry.p2, ip))
                    {
                        f32 d = mag2(ip - ray_pos);
                        if(d < cd)
                        {
                            cj = j;
                            cd = d;
                            cip = ip;
                        }
                    }                    
                }
            }

            // check intersection with inset hull
            u32 num_inset_hull_points = sb_count(net.inset_hull);
            for(u32 j = 0; j < num_inset_hull_points; ++j)
            {
                if(road_sections[i].outer)
                    continue;

                u32 n = (j+1)%num_inset_hull_points;

                vec3f& p1 = road_sections[i].entry.p1;
                vec3f& p2 = road_sections[i].exit.p1;

                // TODO: function
                if(maths::line_vs_line(
                    p1, p2, net.inset_hull[j], net.inset_hull[n], ip))
                {
                    f32 d1 = mag2(road_sections[i].entry.p1 - ip);
                    f32 d2 = mag2(road_sections[i].exit.p1 - ip);

                    if(d1 < d2)
                    {
                        //road_sections[i].entry.p1 = ip;
                    }
                    else
                    {
                        //road_sections[i].exit.p1 = ip;
                    }
                }

                vec3f& pp1 = road_sections[i].entry.p2;
                vec3f& pp2 = road_sections[i].exit.p2;

                if(maths::line_vs_line(
                    pp1, pp2, net.inset_hull[j], net.inset_hull[n], ip))
                {
                    f32 d1 = mag2(road_sections[i].entry.p2 - ip);
                    f32 d2 = mag2(road_sections[i].exit.p2 - ip);

                    if(d1 < d2)
                    {
                        //road_sections[i].entry.p2 = ip;
                    }
                    else
                    {
                        //road_sections[i].exit.p2 = ip;
                    }
                }
            }

            vec3f p1 = road_sections[i].exit.p1 + (road_sections[i].exit.p2 - road_sections[i].exit.p1) * 0.5f;

            if(cj >= 0)
            {
                vec3f p2 = road_sections[cj].entry.p1 + (road_sections[cj].entry.p2 - road_sections[cj].entry.p1) * 0.5f;

                road_sections[i].connector.p1 = p1;
                road_sections[i].connector.p2 = p2;

                road_sections[cj].entry_connector.p1 = p1;
                road_sections[cj].entry_connector.p2 = p2;

                enters.insert(cj);
                road_sections[i].destinations.push_back(cj);
            }
            else
            {
                // intersect with hull
                road_sections[i].connector.p1 = p1;

                vec3f ip;
                if(line_vs_convex_hull(p1, p1 + road_sections[i].dir * 100.0f, net.outer_hull, sb_count(net.outer_hull), ip))
                {
                    road_sections[i].connector.p2 = ip;
                }
            }
        }

        // find sections which have no incomming connections, and extend a tail
        for(u32 i = 0; i < num_sections; ++i)
        {
            if(enters.find(i) != enters.end())
                continue;

            // check tail
            vec3f tail = road_sections[i].entry.p1 + (road_sections[i].entry.p2 - road_sections[i].entry.p1) * 0.5f;

            road_sections[i].entry_connector.p1 = tail;
            road_sections[i].entry_connector.p2 = tail;

            if(line_vs_convex_hull(tail, tail - road_sections[i].dir * 100.0f, net.outer_hull, sb_count(net.outer_hull), ip))
            {
                bool valid = true;
                for(u32 j = 0; j < num_sections; ++j)
                {
                    vec3f ip2;
                    if(maths::line_vs_line(tail, ip, road_sections[j].exit.p1, road_sections[j].exit.p2, ip2))
                    {
                        road_sections[i].entry_connector.p2 = ip2;
                        valid = false;
                        break;
                    }
                }

                if(valid)
                {
                    road_sections[i].entry_connector.p2 = ip;
                }
            }
        }

        // for each section find destinations
        for(u32 i = 0; i < num_sections; ++i)
        {
            auto& out = road_sections[i].connector;
            for(u32 j = 0; j < num_sections; ++j)
            {
                auto& in = road_sections[j].entry_connector;

                vec3f ip;
                if(maths::line_vs_line(out.p1, out.p2, in.p1, in.p2, ip))
                {
                    road_sections[i].destinations.push_back(j);
                    continue;
                }

                if(dist2(in.p2, out.p2) < net.params.inset*net.params.inset)
                {
                    road_sections[i].destinations.push_back(j);
                }
            }
        }

        // build junctions finally
        // for each road section join existing junctions, or create a new junction
        for(u32 i = 0; i < num_sections; ++i)
        {
            add_section_to_junctions(net.junctions, net.road_sections, i);
        }

        extend_outer_road(net);

#if 0
        // for each junction add outer hull corners if we are close
        for(auto& junc : net.junctions)
        {
            for(auto& exit : junc.exits)
            {
                auto& section = net.road_sections[exit];

                u32 num_points = sb_count(net.outer_hull);
                for(u32 i = 0; i < num_points; ++i)
                {
                    f32 d2 = net.params.major_inset*net.params.major_inset*0;
                    if(dist2(section.connector.p2, net.outer_hull[i]) < d2)
                    {
                        junc.corner_points.push_back(net.outer_hull[i]);
                    }
                }
            }
        }

        // for each junction hull, try add close points from the outer 
        u32 junction_count = 0;
        for(auto& junction : net.junctions)
        {
            vec3f* points = nullptr;
            for(auto& exit : junction.exits)
            {
                auto& sec = net.road_sections[exit];
                sb_push(points, sec.exit.p1);
                sb_push(points, sec.exit.p2);
            }

            for(auto& entry : junction.entries)
            {
                auto& sec = net.road_sections[entry];
                sb_push(points, sec.entry.p1);
                sb_push(points, sec.entry.p2);
            }

            for(auto& point : junction.corner_points)
            {
                sb_push(points, point);
            }

            vec3f* hull = nullptr;
            convex_hull_from_points(hull, points, sb_count(points));

            u32 num_points = sb_count(net.outer_hull);
            for(u32 j = 0; j < num_points; ++j)
            {
                vec3f cp = closest_point_on_convex_hull(hull, sb_count(hull), net.outer_hull[j]);

                f32 d2 = net.params.major_inset * net.params.major_inset;
                if(dist2(cp, net.outer_hull[j]) < d2)
                {
                    junction.corner_points.push_back(net.outer_hull[j]);
                }
            }

            sb_free(hull);
        }
#endif
    }

    void generate_curb_mesh(const road_network& net, ecs_scene* scene, u32 cell)
    {
        u32 num_hulls = sb_count(net.valid_sub_hulls);
        for(u32 i = 0; i < num_hulls; ++i)
        {
            edge** edge_strips = nullptr;
            sb_push(edge_strips, nullptr);

            curb(edge_strips, net.valid_sub_hulls[i]);

            pen::renderer_new_frame();

            Str f;
            f.appendf("cell_%i_%i", cell, i);
            mesh_from_strips(f.c_str(), edge_strips);

            auto default_material = get_material_resource(PEN_HASH("default_material"));
            auto geom = get_geometry_resource(PEN_HASH(f.c_str()));
            u32  new_prim = get_new_entity(scene);
            scene->names[new_prim] = f;
            scene->names[new_prim].appendf("%i", new_prim);
            scene->transforms[new_prim].rotation = quat();
            scene->transforms[new_prim].scale = vec3f::one();
            scene->transforms[new_prim].translation = vec3f::zero();
            scene->entities[new_prim] |= e_cmp::transform;
            scene->parents[new_prim] = new_prim;
            instantiate_geometry(geom, scene, new_prim);
            instantiate_material(default_material, scene, new_prim);
            instantiate_model_cbuffer(scene, new_prim);

            pen::thread_sleep_ms(1);
        }
    }

    void generate_road_mesh(const road_network& net, ecs_scene* scene, u32 cell)
    {
        vertex_model* verts = nullptr;

        for(auto& section : net.road_sections)
        {
            vertex_model v[6];
            v[0].pos.xyz = section.entry.p1;
            v[1].pos.xyz = section.exit.p1;
            v[2].pos.xyz = section.entry.p2;

            v[3].pos.xyz = section.entry.p2;
            v[4].pos.xyz = section.exit.p1;
            v[5].pos.xyz = section.exit.p2;

            for(u32 vi = 0; vi < 6; ++vi)
            {
                v[vi].pos.w = 1.0f;
                v[vi].normal = vec4f::unit_y();
                v[vi].tangent = vec4f::unit_x();
                v[vi].bitangent = vec4f::unit_z();

                sb_push(verts, v[vi]);
            }
        }

        Str f;
        f.appendf("road_%i", cell);
        create_primitive_resource_faceted(f, verts, sb_count(verts));

        auto default_material = get_material_resource(PEN_HASH("default_material"));
        auto geom = get_geometry_resource(PEN_HASH(f.c_str()));
        u32  new_prim = get_new_entity(scene);
        scene->names[new_prim] = f;
        scene->names[new_prim].appendf("%i", new_prim);
        scene->transforms[new_prim].rotation = quat();
        scene->transforms[new_prim].scale = vec3f::one();
        scene->transforms[new_prim].translation = vec3f::zero();
        scene->entities[new_prim] |= e_cmp::transform;
        scene->parents[new_prim] = new_prim;
        instantiate_geometry(geom, scene, new_prim);
        instantiate_material(default_material, scene, new_prim);
        instantiate_model_cbuffer(scene, new_prim);

        pen::thread_sleep_ms(1);

        u32 junction_count = 0;
        for(auto& junction : net.junctions)
        {
            //continue;

            vec3f* points = nullptr;
            for(auto& exit : junction.exits)
            {
                auto& sec = net.road_sections[exit];
                sb_push(points, sec.exit.p1);
                sb_push(points, sec.exit.p2);
            }

            for(auto& entry : junction.entries)
            {
                auto& sec = net.road_sections[entry];
                sb_push(points, sec.entry.p1);
                sb_push(points, sec.entry.p2);
            }

            for(auto& point : junction.corner_points)
            {
                sb_push(points, point);
            }

            vec3f* hull = nullptr;
            convex_hull_from_points(hull, points, sb_count(points));

            vec3f centre = get_convex_hull_centre(hull, sb_count(hull));

            vertex_model* junction_verts = nullptr;
            u32 num_points = sb_count(hull);
            for(u32 i = 0; i < num_points; ++i)
            {
                u32 n = (i+1)%num_points;

                vertex_model v[3];
                v[0].pos.xyz = hull[i];
                v[1].pos.xyz = centre;
                v[2].pos.xyz = hull[n];

                for(u32 vi = 0; vi < 3; ++vi)
                {
                    v[vi].pos.w = 1.0f;
                    v[vi].normal = vec4f::unit_y();
                    v[vi].tangent = vec4f::unit_x();
                    v[vi].bitangent = vec4f::unit_z();

                    sb_push(junction_verts, v[vi]);
                }
            }

            Str f;
            f.appendf("junction_%i_%i", cell, junction_count);
            create_primitive_resource_faceted(f, junction_verts, sb_count(junction_verts));

            auto default_material = get_material_resource(PEN_HASH("default_material"));
            auto geom = get_geometry_resource(PEN_HASH(f.c_str()));
            u32  new_prim = get_new_entity(scene);
            scene->names[new_prim] = f;
            scene->names[new_prim].appendf("%i", new_prim);
            scene->transforms[new_prim].rotation = quat();
            scene->transforms[new_prim].scale = vec3f::one();
            scene->transforms[new_prim].translation = vec3f::zero();
            scene->entities[new_prim] |= e_cmp::transform;
            scene->parents[new_prim] = new_prim;
            instantiate_geometry(geom, scene, new_prim);
            instantiate_material(default_material, scene, new_prim);
            instantiate_model_cbuffer(scene, new_prim);
            ++junction_count;
        }

        return;

        // outer road
        vec3f prev_cpn = vec3f::zero();

        std::vector<vec3f> projected;

        vertex_model* outer_road_verts = nullptr;
        u32 num_points = sb_count(net.inner_road_hull);
        for(u32 i = 0; i < num_points; ++i)
        {
            u32 n = (i+1)%num_points;

            vec3f cp = closest_point_on_convex_hull(net.outer_hull, sb_count(net.outer_hull), net.inner_road_hull[i]);
            vec3f cpn = closest_point_on_convex_hull(net.outer_hull, sb_count(net.outer_hull), net.inner_road_hull[n]);

            vertex_model v[6];
            v[0].pos.xyz = net.inner_road_hull[i];
            v[1].pos.xyz = net.inner_road_hull[n];
            v[2].pos.xyz = cpn;

            v[3].pos.xyz = net.inner_road_hull[i];
            v[4].pos.xyz = cpn;
            v[5].pos.xyz = cp;

            projected.push_back(cp);
            projected.push_back(cpn);

            for(u32 vi = 0; vi < 6; ++vi)
            {
                v[vi].pos.w = 1.0f;
                v[vi].normal = vec4f::unit_y();
                v[vi].tangent = vec4f::unit_x();
                v[vi].bitangent = vec4f::unit_z();

                sb_push(outer_road_verts, v[vi]);
            }
        }

        num_points = sb_count(net.outer_hull);
        for(u32 i = 0; i < num_points; ++i)
        {
            sort(projected.begin(), projected.end(), 
                [i, net](const vec3f & a, const vec3f & b) -> bool
            { 
                return dist2(a, net.outer_hull[i]) < dist2(b, net.outer_hull[i]);
            });

            u32 num_verts = sb_count(outer_road_verts);
            for(u32 v = 0; v < num_verts; ++v)
            {
                if(dist2(outer_road_verts[v].pos.xyz, projected[0]) < 0.001)
                {
                    outer_road_verts[v].pos.xyz = net.outer_hull[i];
                }
            }
        }

        Str outer_road_name;
        outer_road_name.appendf("outer_road_%i", cell);
        create_primitive_resource_faceted(
            outer_road_name, outer_road_verts, sb_count(outer_road_verts));

        geom = get_geometry_resource(PEN_HASH(outer_road_name.c_str()));
        new_prim = get_new_entity(scene);
        scene->names[new_prim] = outer_road_name;
        scene->names[new_prim].appendf("%i", new_prim);
        scene->transforms[new_prim].rotation = quat();
        scene->transforms[new_prim].scale = vec3f::one();
        scene->transforms[new_prim].translation = vec3f::zero();
        scene->entities[new_prim] |= e_cmp::transform;
        scene->parents[new_prim] = new_prim;
        instantiate_geometry(geom, scene, new_prim);
        instantiate_material(default_material, scene, new_prim);
        instantiate_model_cbuffer(scene, new_prim);

        sb_free(outer_road_verts);
        sb_free(verts);

    }

    road_network generate_road_network(const voronoi_map* voronoi, u32 cell, const road_network_params& params)
    {
        road_network net;
        net.params = params;

        // jvc build from cell
        const jcv_site* sites = jcv_diagram_get_sites( &voronoi->diagram );
        const jcv_site* site = &sites[cell];
        const jcv_graphedge* jvce = site->edges;
                
        // edge points from jvc come in different winding order
        vec3f* edge_points = nullptr;
        while( jvce )
        {
            vec3f p2 = jcv_to_vec(jvce->pos[1]);
            jvce = jvce->next;

            sb_push(edge_points, p2);
        }
        u32 nep = sb_count(edge_points);

        vec3f* inset_edge_points = nullptr;
        vec3f* cell_perps = nullptr;
        
        vec3f prev_line[2];
        vec3f first_line[2];
        
        f32 inset = 0.5f;
        f32 major_inset = 0.75f;
        
        // get outer hull correctly wound and inset w/ overlaps
        vec3f* outer_hull = nullptr;
        vec3f* inset_overlapped = nullptr;
        for(s32 i = nep-1; i >= 0; i--)
        {
            s32 n = i-1 < 0 ? nep-1 : i-1;
            
            vec3f p1 = edge_points[i];
            vec3f p2 = edge_points[n];
                        
            vec3f vl = normalize(vec3f(p2-p1));
            
            vec3f perp = cross(vl, vec3f::unit_y());
            
            perp *= major_inset;
            
            vec3f inset_edge0 = p1 - perp;
            vec3f inset_edge1 = p2 - perp;
            
            sb_push(inset_overlapped, inset_edge0);
            sb_push(inset_overlapped, inset_edge1);
            sb_push(net.outer_hull, p1);

            sb_push(cell_perps, cross(vl, vec3f::unit_y()));
        }
        
        // find intersection points of the overlapped inset edges
        vec3f* intersection_points = nullptr;
        u32 no = sb_count(inset_overlapped);
        for (u32 i = 0; i < no; i+=2)
        {
            for (u32 j = 0; j < no; j += 2)
            {
                if (i == j)
                    continue;

                vec3f p0 = inset_overlapped[i];
                vec3f p1 = inset_overlapped[i + 1];

                vec3f pn0 = inset_overlapped[j];
                vec3f pn1 = inset_overlapped[j + 1];

                if (mag(p1 - p0) < major_inset)
                    continue;

                if (mag(pn1 - pn0) < major_inset)
                    continue;

                // extend slightly to catch difficult intersections
                vec3f xt = normalize(pn1 - pn0);
                pn0 -= xt * inset;
                pn1 += xt * inset;

                vec3f ip;
                if (maths::line_vs_line(p0, p1, pn0, pn1, ip))
                {
                    sb_push(intersection_points, ip);
                }
            }
        }

        if (!intersection_points)
            PEN_ASSERT(0);

        // make a clean tidy inset hull
        convex_hull_from_points(net.inset_hull, intersection_points, sb_count(intersection_points));
        net.inset_hull = convex_hull_tidy(net.inset_hull, sb_count(net.inset_hull), inset);

        // subdidive and build roads
        get_hull_subdivision_extents(net);
        subdivide_as_grid(net);
        generate_roads_and_junctions(net);

        generate_curb_mesh(net, scene, cell);
        generate_road_mesh(net, scene, cell);

        return net;
    }

    #define if_dbg_checkbox(name, def) static bool name = def; ImGui::Checkbox(#name, &name); if(name)

    void debug_render_road_section(const road_section& section, const std::vector<road_section>& sections)
    {
        add_line(section.exit.p1, section.exit.p2, vec4f::red());
        add_line(section.entry.p1, section.entry.p2, vec4f::green());
        add_line(section.connector.p1, section.connector.p2, vec4f::blue());
        add_line(section.entry_connector.p1, section.entry_connector.p2, vec4f::magenta());
        add_point(section.exit.p1, 0.1f, vec4f::magenta());
        add_point(section.exit.p2, 0.1f, vec4f::magenta());

        add_line(section.exit.p1, section.exit.p2, vec4f::white());
        add_line(section.exit.p2, section.entry.p2, vec4f::white());
    }

    void debug_render_road_sections(const road_network& net)
    {
        for(auto& section : net.road_sections)
        {
            debug_render_road_section(section, net.road_sections);
        }
    }

    void debug_render_road_network(const road_network& net)
    {
        if_dbg_checkbox(outer_hull, true)
            draw_convex_hull(net.outer_hull, sb_count(net.outer_hull), vec4f::white());
        
        if_dbg_checkbox(inner_hull, true)
            draw_convex_hull(net.inset_hull, sb_count(net.inset_hull), vec4f::orange());

        if_dbg_checkbox(extent_axis, false)
            for(u32 i = 0; i < 2; ++i)
                add_line(net.extent_axis_points[i].p1, net.extent_axis_points[i].p2, vec4f::red());

        if_dbg_checkbox(sub_hulls, false)
            for(u32 i = 0; i < sb_count(net.valid_sub_hulls); ++i)
                draw_convex_hull(net.valid_sub_hulls[i], sb_count(net.valid_sub_hulls[i]), vec4f::blue());

        if_dbg_checkbox(inner_road_hull, false)
        {
            draw_convex_hull(net.inner_road_hull, sb_count(net.inner_road_hull), vec4f::cyan());

            u32 num_points = sb_count(net.inner_road_hull);
            for(u32 i = 0; i < num_points; ++i)
            {
                u32 n = (i+1)%num_points;

                vec3f cp = closest_point_on_convex_hull(net.outer_hull, sb_count(net.outer_hull), net.inner_road_hull[i]);

                add_point(cp, 0.1f, vec4f::cyan());
            }
        }

        static s32 join_idx = 0;
        ImGui::InputInt("join", &join_idx);
        u32 join_count = 0;
        for(auto& join : net.joins)
        {
            if(join_count == join_idx)
            {
                add_point(join.v[0], 0.1f, vec4f::magenta());
                add_point(join.v[1], 0.1f, vec4f::green());
                add_point(join.v[2], 0.1f, vec4f::yellow());
            }
            join_count++;
        }


        static s32 dbg_idx = 4;
        ImGui::InputInt("junction", &dbg_idx);
        u32 count = 0;
        for(auto& junc : net.junctions)
        {
            if(count == dbg_idx)
            {
                for(auto& exit : junc.exits)
                {
                    auto& sec = net.road_sections[exit];
                    add_point(sec.exit.p1, 0.1f, vec4f::cyan());
                    add_point(sec.exit.p2, 0.1f, vec4f::cyan());
                }

                for(auto& entry : junc.entries)
                {
                    auto& sec = net.road_sections[entry];
                    add_point(sec.entry.p1, 0.1f, vec4f::green());
                    add_point(sec.entry.p2, 0.1f, vec4f::green());
                }

                for(auto& point : junc.corner_points)
                {
                    add_point(point, 0.2f, vec4f::green());
                }
            }

            ++count;
        }

        debug_render_road_sections(net);
    }

    road_network* nets;
    void generate()
    {
        road_network_params rp;
        rp.inset = 0.5f;
        rp.major_inset = 0.75f;
        rp.subdiv_size = 4.0f;

        for(u32 i = 0; i < voronoi->diagram.numsites; ++i)
        {
            road_network net = generate_road_network(voronoi, i, rp);
            sb_push(nets, net);
            PEN_LOG("generated %i", i);
        }
    }
    
    int on_update(f32 dt)
    {
        if (!g_debug)
            return 0;

        static s32 test_single = 3;
        ImGui::InputInt("Test Single", (s32*)&test_single);
        
        if(test_single != -1)
        {
            debug_render_road_network(nets[test_single]);
        }
        else
        {
            u32 num_nets = sb_count(nets);
            for(u32 i = 0; i < num_nets; ++i)
                debug_render_road_network(nets[i]);
        }

        return 0;
    }
    
    int on_unload()
    {
        return 0;
    }
};

CR_EXPORT int cr_main(struct cr_plugin *ctx, enum cr_op operation)
{
    live_context* live_ctx = (live_context*)ctx->userdata;
    static live_lib ll;
    
    switch (operation)
    {
        case CR_LOAD:
            return ll.on_load(live_ctx);
        case CR_UNLOAD:
            return ll.on_unload();
        case CR_CLOSE:
            return 0;
        default:
            break;
    }
    
    return ll.on_update(live_ctx->dt);
}

namespace pen
{
    pen_creation_params pen_entry(int argc, char** argv)
    {
        return {};
    }
    
    void* user_entry(void* params)
    {
        return nullptr;
    }
}


