include(FetchContent)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.9.1
)

set(JSON_Install OFF)
set(JSON_BuildTests OFF)
FetchContent_MakeAvailable(json)
