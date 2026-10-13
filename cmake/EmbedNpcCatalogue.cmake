# Writes a C++ source with every NPC catalogue table (runtime/assets/npc_catalogue/*.json) as text:
#   cmake -DDIR=dir -DOUT=out.cpp -P EmbedNpcCatalogue.cmake
# defines `const char* const wwhd_npc_catalogue_tables[]` and `const unsigned wwhd_npc_catalogue_table_count`
# (story_npc_catalogue.h). A new place is a new JSON file here; nothing else changes.
file(GLOB tables "${DIR}/*.json")
list(SORT tables)
set(body "")
set(count 0)
foreach(table ${tables})
  file(READ "${table}" text)
  string(FIND "${text}" ")wwhd\"" clash)
  if(NOT clash EQUAL -1)
    message(FATAL_ERROR "${table} contains the raw string delimiter")
  endif()
  get_filename_component(name "${table}" NAME)
  string(APPEND body "    // ${name}\n    R\"wwhd(${text})wwhd\",\n")
  math(EXPR count "${count} + 1")
endforeach()
if(count EQUAL 0)
  set(body "    \"\",\n")
endif()
file(WRITE "${OUT}.tmp" "// generated from ${DIR} by cmake/EmbedNpcCatalogue.cmake\nextern \"C\" const char* const wwhd_npc_catalogue_tables[] = {\n${body}};\nextern \"C\" const unsigned wwhd_npc_catalogue_table_count = ${count};\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
