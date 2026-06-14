# Audio-to-MIDI transcription (issue #1168). The Basic Pitch ONNX backend is
# gated on MAGDA_HAVE_CLAP (the project's "ONNX Runtime available" switch); the
# stub branch in BasicPitchTranscriber.cpp keeps the build linking when it is
# off. BasicPitchPostprocess is pure and always compiled.

target_sources(magda_daw PRIVATE
    BasicPitchPostprocess.cpp
    BasicPitchTranscriber.cpp
    TranscriptionService.cpp
)
