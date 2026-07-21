# Audio source separation (issue #1288). HPSS is pure DSP and always
# compiled. The ONNX engines are gated on MAGDA_HAVE_CLAP (the project's
# "ONNX Runtime available" switch) via a stub branch in each backend's .cpp,
# like the transcription backend. The downloader is gate-free: it only
# fetches files.

target_sources(magda_daw PRIVATE
    DemucsSeparator.cpp
    HpssSeparator.cpp
    SpleeterSeparator.cpp
    StemModelDownloader.cpp
    StemSeparationService.cpp
)
