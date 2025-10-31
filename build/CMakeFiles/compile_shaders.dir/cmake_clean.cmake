file(REMOVE_RECURSE
  "CMakeFiles/compile_shaders"
  "shaders/bloom.frag.spv"
  "shaders/city.frag.spv"
  "shaders/city.vert.spv"
  "shaders/cube.frag.spv"
  "shaders/cube.vert.spv"
  "shaders/debug_chunk.frag.spv"
  "shaders/debug_chunk.vert.spv"
  "shaders/debug_text.frag.spv"
  "shaders/debug_text.vert.spv"
  "shaders/hdr.frag.spv"
  "shaders/neon.frag.spv"
  "shaders/neon.vert.spv"
  "shaders/postprocess.frag.spv"
  "shaders/postprocess.vert.spv"
  "shaders/shadow_darken.frag.spv"
  "shaders/shadow_darken.vert.spv"
  "shaders/shadow_volume.frag.spv"
  "shaders/shadow_volume.vert.spv"
  "shaders/sun.frag.spv"
  "shaders/sun.vert.spv"
  "shaders/vol_anamorphic_bloom.comp.spv"
  "shaders/vol_cluster_build.comp.spv"
  "shaders/vol_density_inject.comp.spv"
  "shaders/vol_light_inject.comp.spv"
  "shaders/vol_raymarch.comp.spv"
  "shaders/vol_temporal.comp.spv"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/compile_shaders.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
