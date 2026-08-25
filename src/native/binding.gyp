{
  "targets": [
    {
      "target_name": "bound_read",
      "sources": ["bound_read.c"],
      "cflags": ["-Wall", "-Wextra", "-Werror", "-std=c11"],
      "xcode_settings": {
        "GCC_GENERATE_DEBUGGING_SYMBOLS": "NO",
        "OTHER_CFLAGS": [
          "-Wall", "-Wextra", "-Werror", "-std=c11",
          "-ffile-prefix-map=<(module_root_dir)=.",
          "-fdebug-prefix-map=<(module_root_dir)=."
        ]
      },
      "conditions": [
        ["OS=='mac'", {
          "defines": ["PLATFORM_DARWIN=1"]
        }],
        ["OS=='linux'", {
          "defines": ["PLATFORM_LINUX=1"]
        }]
      ]
    }
  ]
}
