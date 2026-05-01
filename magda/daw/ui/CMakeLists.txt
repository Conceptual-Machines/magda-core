# UI layer aggregator. Sources are scoped to magda_daw_app; the
# magda_daw library only picks up the few UI components that tests link
# against (timeline + state, see those subdirs).

add_subdirectory(themes)
add_subdirectory(windows)
add_subdirectory(dialogs)
add_subdirectory(debug)
add_subdirectory(panels)
add_subdirectory(views)
add_subdirectory(layout)
add_subdirectory(state)
add_subdirectory(components)
