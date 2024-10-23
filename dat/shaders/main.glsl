#ifdef VERTEX_SHADER
#define VS2FS out
#else
#define VS2FS in
#endif

uniform vec2 screen_size;
uniform vec3 camera_position;
uniform vec3 camera_bottom_left;
uniform vec3 camera_bottom_right;
uniform vec3 camera_top_left;
uniform vec3 camera_top_right;
uniform sampler2D mip_texture;
uniform bool enable_mips;
uniform int max_main_iterations;
uniform int max_shadow_iterations;
uniform float shadow_threshold;

#ifdef VERTEX_SHADER

void main() {
	vec2[] positions = vec2[](
		vec2(-1, -1),
		vec2(-1,  1),
		vec2( 1, -1),
		vec2( 1, -1),
		vec2(-1,  1),
		vec2( 1,  1)
	);

	vec2 position = positions[gl_VertexID];

	gl_Position = vec4(position, 0, 1);
}

#endif

#ifdef FRAGMENT_SHADER

#include common.glsli

vec3 scene_normal(vec3 p) {
	vec2 e = vec2(1e-3, 0);
	vec3 n = scene_distance(p) - 
		vec3(scene_distance(p-e.xyy), 
			 scene_distance(p-e.yxy),
			 scene_distance(p-e.yyx));
	
	return normalize(n);
}

struct RaymarchResult {
	vec3 p;
	float d;
};

RaymarchResult ray_march(vec3 ro, vec3 rd, float initial_d, int max_iterations) {
	RaymarchResult r; 
	
	r.d = initial_d;
	
	for (int i = 0; i < max_iterations; ++i) {
		vec3 p = ro + rd*r.d;
		float dS = scene_distance(p);
		r.d += dS;
		if(r.d>MAX_DIST || abs(dS)<SURF_DIST) break;
	}
	
	r.p = ro + rd * r.d;
	
	return r;
}

// https://www.shadertoy.com/view/ltKcWc
float genAmbientOcclusion(vec3 ro, vec3 rd) {
	float totao = 0;
	float sca = 1.0;

	for (int aoi = 0; aoi < 5; aoi++)
	{
		float hr = 0.01 + 0.02 * float(aoi * aoi);
		vec3 aopos = ro + rd * hr;
		float dd = scene_distance(aopos);
		float ao = clamp(-(dd - hr), 0.0, 1.0);
		totao += ao * sca;
		sca *= 0.75;
	}

	const float aoCoef = 1;
	totao = 1.0 - clamp(aoCoef * totao, 0.0, 1.0);

	return totao;
}

// https://typhomnt.github.io/teaching/ray_tracing/raymarching_intro/#bonus-effect-ambient-occulsion
float AmbientOcclusion(vec3 point, vec3 normal, float step_dist, float step_nbr)
{
	float occlusion = 1.0f;
	while(step_nbr > 0.0)
	{
		occlusion -= pow(step_nbr * step_dist - scene_distance(point + normal * step_nbr * step_dist),2) / step_nbr;
		step_nbr--;
	}

	return clamp(occlusion, 0, 1);
}

vec3 perp(vec3 v) {
	vec3 up = vec3(0,1,0);
	if (dot(up, v) < 0.999) {
		return normalize(cross(v, up));
	}
	return normalize(cross(v, vec3(1,0,0)));
}

vec3 perph(vec3 v, vec3 p) {
	vec3 up = normalize(random_vec3(p * 1000));
	if (dot(up, v) < 0.999) {
		return normalize(cross(v, up));
	}
	return normalize(cross(v, vec3(1,0,0)));
}

float sum_ao(vec3 p, vec3 n) {

	const vec3[] ao_dirs = vec3[](
		normalize(vec3(0, 0, 1)),
		normalize(vec3(cos(TAU/6*0), sin(TAU/6*0), 1)),
		normalize(vec3(cos(TAU/6*1), sin(TAU/6*1), 1)),
		normalize(vec3(cos(TAU/6*2), sin(TAU/6*2), 1)),
		normalize(vec3(cos(TAU/6*3), sin(TAU/6*3), 1)),
		normalize(vec3(cos(TAU/6*4), sin(TAU/6*4), 1)),
		normalize(vec3(cos(TAU/6*5), sin(TAU/6*5), 1))
	);

	float ao = 0;
	for (int i = 0; i < ao_dirs.length(); ++i) {
		vec3 p = perph(n, p);
		mat3 m = mat3(p, cross(p, n), n);
		RaymarchResult r = ray_march(p, m * ao_dirs[i], 0, 8);
		if (r.d > 5.0f)
			ao += 1;
	}
	return ao / ao_dirs.length();
}

#define FOG_END 500

out vec4 frag_color;
void main() {
	vec2 uv = gl_FragCoord.xy / screen_size;

	vec3 ro = camera_position;
	vec3 rd = normalize(mix(
		mix(camera_bottom_left, camera_top_left, uv.y), 
		mix(camera_bottom_right, camera_top_right, uv.y), uv.x));

	RaymarchResult r = ray_march(ro, rd, enable_mips ? texture(mip_texture, uv).r : 0, max_main_iterations);
	vec3 N = scene_normal(r.p);
	vec3 L = normalize(vec3(2,3,1));
	vec3 V = rd;
	vec3 H = normalize(L-V);
	
	float ao_radius = 1;
	float ao = mapc(scene_distance(r.p + N * ao_radius) / ao_radius, 0, 1, 0.1, 1);
	//float ao = genAmbientOcclusion(r.p, N);
	//float ao = AmbientOcclusion(r.p, N, 1, 1);
	//float ao = sum_ao(r.p, N);

	vec3 ambient = vec3(.3,.6,.9);
	vec3 albedo = vec3(0.8);
	float shiny = 0.8;

	vec3 dif = albedo * max(0.,dot(N, L));
	vec3 indirect = albedo * ambient / PI * ao;
	float specular = pow(max(0.001, dot(N, H)), shiny * 100.) * pow(shiny, 2.0);

	RaymarchResult s = ray_march(r.p+N*SURF_DIST*2., L, 0, max_shadow_iterations);
	if(s.d < shadow_threshold) {
		dif *= 0.;
		specular *= 0.;
	}
		
		
	vec3 c = dif + specular + indirect;
	
	vec3 sky = ambient + vec3(0.8, 1.0, 0.6) * smoothstep(0.0, 1.0, dot(V, L));
	sky = vec3(1.2, 1.4, 1.0) * pow(ambient, vec3((-dot(V,L)+1.) * 2.));

	c = mix(c, sky, pow(clamp(r.d/FOG_END, 0.0, 1.0), 2));

	c = pow(c, vec3(.4545));	// gamma correction
	
	//c = vec3(texture(mip_texture, uv).r) * 0.01;



	frag_color = vec4(c, 1);
}

#endif
