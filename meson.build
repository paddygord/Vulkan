project('render', 'cpp',
  default_options: [
    'prefix=' + meson.source_root() / 'packaged',
    'buildtype=debugoptimized',
    'warning_level=3',
    'cpp_std=c++17',
  ]
)

add_project_arguments(
  '-Wno-unused-parameter',
  '-Wno-non-virtual-dtor',
  '-Wno-unused-but-set-variable',
  '-Wno-sign-compare',
  '-Wno-type-limits',
  '-Wno-ignored-qualifiers',
  language: 'cpp',
)

gli_dep = declare_dependency(
  include_directories: [
    'gli',
    'gli/gli',
  ],
)
imgui_lib = static_library(
  'imgui',
  sources: [
    'imgui/imgui.cpp',
    'imgui/imgui_demo.cpp',
    'imgui/imgui_draw.cpp',
    'imgui/imgui_tables.cpp',
    'imgui/imgui_widgets.cpp',
  ],
)
imgui_dep = declare_dependency(
  include_directories: 'imgui',
  link_with: imgui_lib,
)

common_deps = [
  dependency('glfw3'),
  dependency('glm'),
  dependency('freetype2'),
  dependency('assimp'),
  gli_dep,
  imgui_dep,
]

vulkan_deps = [
  meson.get_compiler('cpp').find_library('dl', required: false),
  declare_dependency(
    link_args: ['-lglslang'],
  ),
  declare_dependency(
    link_args: ['-lSPIRV'],
  ),
  dependency('vulkan'),
  dependency('xcb'),
]

base_sources = [
    'base/android.cpp',
    'base/common.cpp',
    'base/glfw/glfw.cpp',
    'base/pbr.cpp',
    'base/shapes.cpp',
    'base/ui.cpp',
    'base/utils.cpp',
    'base/vks/context.cpp',
    'base/vks/debug.cpp',
    'base/vks/filesystem.cpp',
    'base/vks/model.cpp',
    'base/vks/shaders.cpp',
    'base/vks/storage.cpp',
    'base/vks/vks.cpp',
    'base/vulkanExampleBase.cpp',
    'base/vulkanGear.cpp',
]

examples = [
  #'bloom',
  'computecloth',
  'computecullandlod',
  'computeheadless',
  'computenbody',
  'computeparticles',
  'computeshader',
  #'conservativeraster',
  'context',
  'debugmarker',
  #'deferred',
  #'deferredmultisampling',
  #'deferredshadows',
  'descriptorsets',
  'displacement',
  'distancefieldfonts',
  'dynamicuniformbuffer',
  'gears',
  'geometryshader',
  'glinterop',
  #'hdr',
  'indirect',
  'indirectdraw',
  'instancing',
  'mesh',
  'multisampling',
  #'multiview',
  'occlusionquery',
  #'offscreen',
  'parallaxmapping',
  'particlefire',
  'pbrbasic',
  'pbribl',
  'pbrtexture',
  'pipelines',
  'pipelinestatistics',
  'pushconstants',
  'pushdescriptors',
  #'radialblur',
  'raytracing',
  'renderheadless',
  'scenerendering',
  'screenshot',
  #'shadowmapping',
  #'shadowmappingcascade',
  #'shadowmappingomni',
  'skeletalanimation',
  'specializationconstants',
  'sphericalenvmapping',
  #'ssao',
  'stencilbuffer',
  'subpasses',
  'swapchain',
  'terraintessellation',
  'tessellation',
  'texture',
  'texture3d',
  'texturearray',
  'texturecubemap',
  'texturesparseresidency',
  'triangle',
  'triangleAnimated',
  'triangleRevisited',
  'viewportarray',
  #'vr_oculus',
  #'vr_openvr',
  'vulkanscene',
]

foreach example: examples
  executable(
    example,
    sources: [
      'examples/' + example + '/' + example + '.cpp',
    ] + base_sources,
    include_directories: [
      'base',
    ],
    dependencies: common_deps + vulkan_deps,
    cpp_args: [
      '-DVK_USE_PLATFORM_XCB_KHR',
      '-DGLM_FORCE_DEPTH_ZERO_TO_ONE',
    ],
    install: true,
  )
endforeach
