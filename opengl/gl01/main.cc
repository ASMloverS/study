// Copyright (c) 2019 ASMlover. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list ofconditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materialsprovided with the
//    distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include <fstream>
#include <iostream>
#include <sstream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

static constexpr int kDefWidth = 800;
static constexpr int kDefHeight = 449;

static void process_input(GLFWwindow* window) {
  if (::glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    ::glfwSetWindowShouldClose(window, true);
}

int main(int argc, char* argv[]) {
  (void)argc, (void)argv;

  ::glfwInit();
  ::glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  ::glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  ::glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__) || defined(__MACH__)
  ::glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  GLFWwindow* window = ::glfwCreateWindow(
      kDefWidth, kDefHeight, "LearnOpenGL", nullptr, nullptr);
  if (window == nullptr) {
    std::cerr << "failed to create GLFW window" << std::endl;
    ::glfwTerminate();
    return -1;
  }
  ::glfwMakeContextCurrent(window);

  if (!::gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "failed to initialize GLAD" << std::endl;
    ::glfwTerminate();
    return -1;
  }

  ::glfwSetFramebufferSizeCallback(window,
      [](GLFWwindow*, int w, int h) { ::glViewport(0, 0, w, h); });

  auto compile_shader_fn = [](const char* fname, int shader_type) -> int {
    std::ifstream fp(fname);
    std::stringstream ss;
    ss << fp.rdbuf();
    auto s = ss.str();
    const char* source = s.data();

    int shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int succ;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &succ);
    if (!succ) {
      char info[512]{};
      glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
      std::cerr << "ERROR: compile shader(" << fname << ")" << info << std::endl;
    }

    return shader;
  };

  // build and compile our shader program
  int vshader = compile_shader_fn("D:/Repositories/study/opengl/gl01/vertex.fx", GL_VERTEX_SHADER);
  int fshader = compile_shader_fn("D:/Repositories/study/opengl/gl01/fragment.fx", GL_FRAGMENT_SHADER);

  // link shaders
  int shader_prog = glCreateProgram();
  glAttachShader(shader_prog, vshader);
  glAttachShader(shader_prog, fshader);
  glLinkProgram(shader_prog);
  int succ;
  glGetProgramiv(shader_prog, GL_LINK_STATUS, &succ);
  if (!succ) {
    char info[512]{};
    glGetProgramInfoLog(shader_prog, sizeof(info), nullptr, info);
    std::cerr << "ERROR: " << info << std::endl;
  }
  glDeleteShader(vshader);
  glDeleteShader(fshader);

  float vertices[] = {
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
     0.0f,  0.5f, 0.0f
  };
  unsigned int VBO, VAO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  while (!::glfwWindowShouldClose(window)) {
    process_input(window);

    // render
    ::glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    ::glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shader_prog);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    ::glfwPollEvents();
    ::glfwSwapBuffers(window);
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);

  ::glfwTerminate();
  return 0;
}
