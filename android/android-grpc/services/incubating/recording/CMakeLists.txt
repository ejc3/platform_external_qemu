android_grpc_service_lib(
  TARGET screen-recording-service SOURCE_DIR
         ${AEMU_PROTO_DIR}/services/incubating/recording
  SOURCES screen_recording_service.proto)

# The ScreenRecording service is no longer hosted by the emulator (it allowed
# remote clients to write arbitrary host files, b/511540723). Only the proto
# and client library remain for existing clients.
add_subdirectory(client)
