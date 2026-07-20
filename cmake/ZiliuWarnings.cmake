function(ziliu_configure_target target)
  target_compile_definitions(
    ${target}
    PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

  if(MSVC)
    target_compile_options(
      ${target}
      PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
  endif()
endfunction()

