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
uniform float cone_angle;
uniform float tan_of_cone_angle;
uniform sampler2D mip_texture;
uniform int max_mip_iterations;

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

struct RaymarchResult {
	vec3 p;
	float d;
};

RaymarchResult ray_march(vec3 ro, vec3 rd, float initial_d) {
	RaymarchResult r; 
	
	r.d = initial_d;
	
	for(int i=0; i<max_mip_iterations; i++) {
		vec3 p = ro + rd*r.d;
		float cone_radius = r.d * tan_of_cone_angle;
		float dS = scene_distance(p) - cone_radius;
		r.d += dS;
		if(r.d>MAX_DIST || dS<SURF_DIST) break;
	}
	
	r.p = ro + rd * r.d;
	
	return r;
}

out vec4 frag_color;
void main() {
	vec2 uv = gl_FragCoord.xy / screen_size;

	vec3 ro = camera_position;
	vec3 rd = normalize(mix(
		mix(camera_bottom_left, camera_top_left, uv.y), 
		mix(camera_bottom_right, camera_top_right, uv.y), uv.x));

	RaymarchResult r = ray_march(ro, rd, texture(mip_texture, uv).r);

	frag_color = vec4(r.d, 0, 0, 0);
}

#endif
