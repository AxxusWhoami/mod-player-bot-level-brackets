CollectSourceFiles(
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  PRIVATE_SOURCES
)

if (PRIVATE_SOURCES)
  GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

  add_library(mod-player-bot-level-brackets STATIC
    ${PRIVATE_SOURCES}
  )

  target_link_libraries(mod-player-bot-level-brackets
    PRIVATE
      game-interface
  )

  target_include_directories(mod-player-bot-level-brackets
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
endif()
