#define TL_IMPL
#include <tl/main.h>
#include <tl/win32.h>
#include <tl/default_logger.h>
#include <tl/opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_opengl3.h>

ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }

using namespace tl;

using String = Span<utf8>;

String program_directory;

HWND hwnd;
v2u screen_size;
bool screen_size_changed;

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
	
	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
		return true;


	switch (msg) {
		case WM_CLOSE: {
			PostQuitMessage(0);
			return 0;
		}
		case WM_SIZE: {
			screen_size = {LOWORD(lp), HIWORD(lp)};
			screen_size_changed = true;
			break;
		}
	}
	return DefWindowProc(hwnd, msg, wp, lp);
}

void bind_texture(GLuint program, u32 slot, char const *name, GLuint texture, GLuint sampler = 0, GLenum target = GL_TEXTURE_2D) {
	glActiveTexture(GL_TEXTURE0 + slot); 
	glBindTexture(target, texture); 
	gl::set_uniform(program, name, (int)slot); 
	glBindSampler(slot, sampler);
}

s32 tl_main(Span<String> args) {
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);

	program_directory = parse_path(args[0]).directory;

	DefaultLogger::global_init(tformat(u8"{}\\log.txt"s, program_directory));
	DefaultLogger logger {.module = u8"app"s};
	current_logger = logger;

	DefaultLogger tl_logger {.module = u8"tl"s};
	tl::tl_logger = tl_logger;

	hwnd = create_class_and_window(u8"raymarch-template"s, wnd_proc, u8"Raymarch Template"s);

	gl::init_opengl((NativeWindowHandle)hwnd);
	
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigDragClickToInputText = true;
	
	auto &style = ImGui::GetStyle();
	style.HoverDelayShort = 2.0f;
	
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_InitForOpenGL(hwnd);
	ImGui_ImplOpenGL3_Init();

	gl::ProgramFile main_shader_program = {.path = format(u8"{}/../dat/shaders/main.glsl"s, program_directory)};
	gl::ProgramFile cone_march_shader_program = {.path = format(u8"{}/../dat/shaders/cone_march.glsl"s, program_directory)};

	gl::ProgramFile *all_programs[] = {&main_shader_program, &cone_march_shader_program};

	for (auto program : all_programs) {
		program->load();
	}

	f32 aspect_ratio = 1;

	constexpr f32 mip_factor = 4;

	struct Mip {
		v2u size = {};
		f32 cone_angle = 0;
		f32 tan_of_cone_angle = 0;
		GLuint color_texture = 0;
		GLuint frame_buffer = 0;
	};

	constexpr int MAX_MIPS = 12;
	Mip mips[MAX_MIPS] = {};
	u32 usable_mips_count = 0;

	auto update_mips = [&] {
		umm mip_index = 0;

		v2u min_size = {1, 1};

		v2u current_size = screen_size;
		while (any(current_size != min_size)) {
			defer { ++mip_index; };

			if (mip_index == MAX_MIPS) {
				break;
			}

			current_size /= mip_factor;
			current_size = max(current_size, min_size);

			Mip &mip = mips[mip_index];
			
			mip.size = current_size;
			mip.cone_angle = angle(v3f{0, 0, -1}, v3f{aspect_ratio / mip.size.x * 2, 1.0f / mip.size.y * 2, -1});
			mip.tan_of_cone_angle = tan(mip.cone_angle);

			if (!mip.color_texture) {
				glGenTextures(1, &mip.color_texture);
				glBindTexture(GL_TEXTURE_2D, mip.color_texture);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
				glBindTexture(GL_TEXTURE_2D, 0);
	
				glGenFramebuffers(1, &mip.frame_buffer);
				glBindFramebuffer(GL_FRAMEBUFFER, mip.frame_buffer);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip.color_texture, 0);
				glReadBuffer(GL_NONE);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
			}
			
			glBindTexture(GL_TEXTURE_2D, mip.color_texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mip.size.x, mip.size.y, 0, GL_RED, GL_UNSIGNED_BYTE, 0);
			glBindTexture(GL_TEXTURE_2D, 0);
		}

		usable_mips_count = mip_index;

		reverse_in_place(Span(&mips[0], (umm)usable_mips_count));
		for (umm i = 0; i < usable_mips_count; ++i) {
			auto &mip = mips[i];
			println("mips[{}].size = {} .cone_angle = {} .tan_of_cone_angle = {}", i, mip.size, mip.cone_angle, mip.tan_of_cone_angle);
		}
	};

	v3f camera_position = {0, 25, 0};
	v3f camera_angles = {pi*0.25f, 0, 0};
	
	f32 frame_time = 1.0f / 60;
	f64 time = 0;
	PreciseTimer frame_timer = create_precise_timer();

	f32 frame_times[60] = {};
	u32 frame_times_index = 0;

	int mip_count = 2;
	int max_mip_iterations = 64;
	int max_main_iterations = 64;
	int max_shadow_iterations = 64;
	f32 shadow_threshold = 1e+2;
	
	f32 MAX_DIST = 1e3;
	f32 SURF_DIST = 1e-3;

	bool enable_vsync = true;
	wglSwapIntervalEXT(enable_vsync);

	make_os_timing_precise();

	MSG msg{};
	while (1) {
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT) {
				return 0;
			}
		}

		for (auto program : all_programs) {
			if (program->needs_reload()) {
				program->unload();
				program->load();
			}
		}
		
		if (screen_size_changed) {
			screen_size_changed = false;
			aspect_ratio = (f32)screen_size.x / screen_size.y;
			update_mips();
		}

		static ImVec2 prev_mouse_position;
		ImVec2 mouse_position = ImGui::GetMousePos();
		ImVec2 mouse_delta = {mouse_position.x - prev_mouse_position.x, mouse_position.y - prev_mouse_position.y};
		prev_mouse_position = mouse_position;
		
		f32 speed = 5;
		if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) speed *= 10;
		if (ImGui::IsKeyDown(ImGuiKey_LeftAlt)) speed /= 10;

		camera_position += m3::rotation_r_zxy(-camera_angles) * (frame_time * speed * v3f {
			(f32)(ImGui::IsKeyDown(ImGuiKey_D) - ImGui::IsKeyDown(ImGuiKey_A)),
			(f32)(ImGui::IsKeyDown(ImGuiKey_E) - ImGui::IsKeyDown(ImGuiKey_Q)),
			(f32)(ImGui::IsKeyDown(ImGuiKey_S) - ImGui::IsKeyDown(ImGuiKey_W)),
		});
		
		v3f camera_bottom_left  = m3::rotation_r_zxy(-camera_angles) * v3f{-aspect_ratio, -1, -1};
		v3f camera_bottom_right = m3::rotation_r_zxy(-camera_angles) * v3f{+aspect_ratio, -1, -1};
		v3f camera_top_left     = m3::rotation_r_zxy(-camera_angles) * v3f{-aspect_ratio, +1, -1};
		v3f camera_top_right    = m3::rotation_r_zxy(-camera_angles) * v3f{+aspect_ratio, +1, -1};

		if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && !ImGui::GetIO().WantCaptureMouse) {
			camera_angles.x += mouse_delta.y * 0.003f;
			camera_angles.y += mouse_delta.x * 0.003f;
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		
		if (ImGui::Begin("Stuff")) {
			ImGui::Text("FPS: %.1f\nResolution: %dx%d", 1.0f / (sum(array_as_span(frame_times)) / count_of(frame_times)), screen_size.x, screen_size.y);
			if (ImGui::Checkbox("VSync", &enable_vsync)) {
				wglSwapIntervalEXT(enable_vsync);
			}
			ImGui::SliderFloat("Surface threshold", &SURF_DIST, 1e-3, 1, "%.3f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Max march distance", &MAX_DIST, 1e1, 1e4, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderInt("Mip count", &mip_count, 0, usable_mips_count);
			ImGui::SliderInt("Max mip iterations", &max_mip_iterations, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderInt("Max main iterations", &max_main_iterations, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderInt("Max shadow iterations", &max_shadow_iterations, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Shadow threshold", &shadow_threshold, 0, 1e+4, "%.1f", ImGuiSliderFlags_Logarithmic);
		}
		ImGui::End();

		ImGui::Render();
		
		auto current_mips = Span(max(&mips[0], mips + usable_mips_count - mip_count), mips + usable_mips_count);
		for (auto &mip : current_mips) {
			glBindFramebuffer(GL_FRAMEBUFFER, mip.frame_buffer);
			gl::viewport(mip.size);
			glUseProgram(cone_march_shader_program.program);
			gl::set_uniform(cone_march_shader_program.program, "screen_size", (v2f)mip.size);
			gl::set_uniform(cone_march_shader_program.program, "camera_position", camera_position);
			gl::set_uniform(cone_march_shader_program.program, "camera_bottom_left", camera_bottom_left);
			gl::set_uniform(cone_march_shader_program.program, "camera_bottom_right", camera_bottom_right);
			gl::set_uniform(cone_march_shader_program.program, "camera_top_left", camera_top_left);
			gl::set_uniform(cone_march_shader_program.program, "camera_top_right", camera_top_right);
			gl::set_uniform(cone_march_shader_program.program, "cone_angle", mip.cone_angle);
			gl::set_uniform(cone_march_shader_program.program, "tan_of_cone_angle", mip.tan_of_cone_angle);
			gl::set_uniform(cone_march_shader_program.program, "max_mip_iterations", max_mip_iterations);
			gl::set_uniform(cone_march_shader_program.program, "SURF_DIST", SURF_DIST);
			gl::set_uniform(cone_march_shader_program.program, "MAX_DIST", MAX_DIST);
			bind_texture(main_shader_program.program, 0, "mip_texture", &mip == &current_mips[0] ? 0 : (&mip)[-1].color_texture);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		gl::viewport(screen_size);
		glUseProgram(main_shader_program.program);
		gl::set_uniform(main_shader_program.program, "screen_size", (v2f)screen_size);
		gl::set_uniform(main_shader_program.program, "camera_position", camera_position);
		gl::set_uniform(main_shader_program.program, "camera_bottom_left", camera_bottom_left);
		gl::set_uniform(main_shader_program.program, "camera_bottom_right", camera_bottom_right);
		gl::set_uniform(main_shader_program.program, "camera_top_left", camera_top_left);
		gl::set_uniform(main_shader_program.program, "camera_top_right", camera_top_right);
		gl::set_uniform(main_shader_program.program, "enable_mips", mip_count != 0);
		gl::set_uniform(main_shader_program.program, "max_main_iterations", max_main_iterations);
		gl::set_uniform(main_shader_program.program, "max_shadow_iterations", max_shadow_iterations);
		gl::set_uniform(main_shader_program.program, "shadow_threshold", shadow_threshold);
		gl::set_uniform(main_shader_program.program, "SURF_DIST", SURF_DIST);
		gl::set_uniform(main_shader_program.program, "MAX_DIST", MAX_DIST);
		bind_texture(main_shader_program.program, 0, "mip_texture", current_mips.count ? current_mips.back().color_texture : 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		gl::present();

		frame_time = reset(frame_timer);
		time += frame_time;

		if (frame_times[0] == 0) {
			for (auto &t : frame_times) {
				t = frame_time;
			}
		} else {
			frame_times[frame_times_index] = frame_time;
			frame_times_index = (frame_times_index + 1) % count_of(frame_times);
		}
	}

	// FPS:
	// w0/ cone marching: 15.0
	// w/ cone marching: 14.5

	return 0;
}

