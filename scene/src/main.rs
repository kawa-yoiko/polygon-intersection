mod gl;
mod scene_loader;

use glfw::Context;
use wavefront_obj::obj;

use core::mem::{size_of, size_of_val};

fn check_gl_errors() {
  let err = gl::GetError();
  if err != 0 {
    panic!("OpenGL error: {}", err);
  }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
  let mut glfw = glfw::init(glfw::FAIL_ON_ERRORS).unwrap();

  glfw.window_hint(glfw::WindowHint::ContextVersion(3, 3));
  glfw.window_hint(glfw::WindowHint::OpenGlForwardCompat(true));
  glfw.window_hint(glfw::WindowHint::OpenGlProfile(glfw::OpenGlProfileHint::Core));

  let (mut window, events) = glfw.create_window(
    960, 540,
    "Window",
    glfw::WindowMode::Windowed,
  )
    .expect("Cannot open window -- check graphics driver");

  gl::load_with(|s| window.get_proc_address(s) as *const _);
  glfw.set_swap_interval(glfw::SwapInterval::Sync(1));

  window.set_key_polling(true);
  window.make_current();

  let mut vao = 0;
  gl::GenVertexArrays(1, &mut vao);
  gl::BindVertexArray(vao);
  let mut vbo = 0;
  gl::GenBuffers(1, &mut vbo);
  gl::BindBuffer(gl::ARRAY_BUFFER, vbo);

  // Load frame
  let frame = scene_loader::load("1a/1a_000001.obj")?;
  gl::VertexAttribPointer(
    0,
    3, gl::FLOAT, gl::FALSE,
    size_of_val(&frame.vertices[0]) as gl::int,
    0 as *const _,
  );
  gl::EnableVertexAttribArray(0);
  gl::BufferData(
    gl::ARRAY_BUFFER,
    size_of_val(&*frame.vertices) as isize,
    frame.vertices.as_ptr().cast(),
    gl::STREAM_DRAW,
  );

  let vs = gl::CreateShader(gl::VERTEX_SHADER);
  const VERTEX_SHADER: &str = r"
#version 330 core
layout (location = 0) in vec3 pos;
// layout (location = 1) in vec3 v_colour_i;
// out vec3 v_colour;
uniform mat4 VP;
void main() {
  gl_Position = VP * vec4(pos.x, pos.y, pos.z, 1.0);
  // v_colour = v_colour_i;
}
";
  gl::ShaderSource(
    vs, 1,
    &(VERTEX_SHADER.as_bytes().as_ptr().cast()),
    &(VERTEX_SHADER.len() as gl::int),
  );
  gl::CompileShader(vs);

  let fs = gl::CreateShader(gl::FRAGMENT_SHADER);
  const FRAGMENT_SHADER: &str = r"
#version 330 core
// in vec3 v_colour;
out vec4 colour;

void main() {
  // colour = vec4(v_colour, 1.0);
  colour = vec4(0.9, 0.8, 0.7, 1.0);
}
";
  gl::ShaderSource(
    fs, 1,
    &(FRAGMENT_SHADER.as_bytes().as_ptr().cast()),
    &(FRAGMENT_SHADER.len() as gl::int),
  );
  gl::CompileShader(fs);

  let prog = gl::CreateProgram();
  gl::AttachShader(prog, vs);
  gl::AttachShader(prog, fs);
  gl::LinkProgram(prog);
  gl::DeleteShader(vs);
  gl::DeleteShader(fs);

  gl::UseProgram(prog);

  check_gl_errors();

  // Camera
  let cam_pos = (9.02922, -8.50027, 7.65063);
  // let cam_ori = (0.780483, 0.483536, 0.208704, 0.336872);
  let cam_look = (3.27, -2.79, 3.62);

  let uni_vp = gl::GetUniformLocation(prog, "VP".as_ptr().cast());
/*
  let mut mvp = [[0f32; 4]; 4];
  mvp[0][0] = 1.0;
  mvp[1][1] = 2.0;
  mvp[2][2] = 1.0;
  mvp[3][3] = 1.0;
*/
  let v_mat = glm::ext::look_at(
    glm::Vector3::new(cam_pos.0, cam_pos.1, cam_pos.2),
    glm::Vector3::new(cam_look.0, cam_look.1, cam_look.2),
    glm::Vector3::new(0.0, 0.0, 1.0),
  );
  let p_mat = glm::ext::perspective(
    0.6911,
    16.0 / 9.0,
    0.1,
    100.0,
  );
  let vp_mat = p_mat * v_mat;
  let mut vp = [[0f32; 4]; 4];
  for i in 0..4 {
    for j in 0..4 {
      vp[i][j] = vp_mat[i][j];
    }
  }
  println!("{:?}", p_mat);
  println!("{:?}", v_mat);
  println!("{:?}", vp);
  gl::UniformMatrix4fv(uni_vp, 1, gl::FALSE, vp.as_ptr().cast());

  gl::Disable(gl::CULL_FACE);

  while !window.should_close() {
    window.swap_buffers();

    glfw.poll_events();
    for (_, event) in glfw::flush_messages(&events) {
      match event {
        glfw::WindowEvent::Key(glfw::Key::Escape, _, glfw::Action::Press, _) => {
          window.set_should_close(true)
        }
        _ => {}
      }
    }

    gl::ClearColor(1.0, 0.99, 0.99, 1.0);
    gl::Clear(gl::COLOR_BUFFER_BIT);

    gl::DrawArrays(gl::TRIANGLES, 0, frame.vertices.len() as gl::int);
    check_gl_errors();
  }

  Ok(())
}