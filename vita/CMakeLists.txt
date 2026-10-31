cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(psv_agent_remote C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Agent Remote")
set(VITA_TITLEID  "CLDR00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

file(GLOB MQTT_PACKET_SRCS third_party/MQTTPacket/*.c)

add_executable(${PROJECT_NAME}
  src/main.c
  src/util.c
  src/config.c
  src/protocol.c
  src/model.c
  src/textlayout.c
  src/markdown.c
  src/psk.c
  src/input.c
  src/settings.c
  src/sessionlist.c
  src/reply.c
  src/view.c
  src/outbox.c
  src/reconnect.c
  src/screen.c
  src/mqtt_client.c
  src/net.c
  src/audio.c
  src/ui.c
  src/gfx.c
  third_party/cJSON/cJSON.c
  ${MQTT_PACKET_SRCS}
)

target_include_directories(${PROJECT_NAME} PRIVATE src third_party/cJSON third_party/MQTTPacket)
set_source_files_properties(third_party/cJSON/cJSON.c ${MQTT_PACKET_SRCS} PROPERTIES COMPILE_FLAGS -w)

target_compile_definitions(${PROJECT_NAME} PRIVATE MQTT_TLS)

target_link_libraries(${PROJECT_NAME}
  mbedtls
  mbedx509
  mbedcrypto
  pthread
  vita2d
  freetype
  png
  jpeg
  z
  bz2
  m
  c
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  SceTouch_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceNet_stub
  SceNetCtl_stub
  SceAudio_stub
  ScePower_stub
  SceIme_stub
  SceAppMgr_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/font.otf fonts/cjk.otf
  FILE assets/sans.ttf fonts/sans.ttf
  FILE assets/sans-medium.ttf fonts/sans-medium.ttf
  FILE assets/mono.ttf fonts/mono.ttf
  FILE assets/mono-medium.ttf fonts/mono-medium.ttf
  FILE assets/agents/claude.png icons/claude.png
  FILE assets/agents/codex.png icons/codex.png
  FILE assets/agents/kiro.png icons/kiro.png
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
