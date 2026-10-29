file(MAKE_DIRECTORY "${WORK}")
execute_process(COMMAND "${JAVAC}" -d "${WORK}"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/CombatMacro.java"
  "${SOURCE}/android/app/src/main/java/org/wwhdrecomp/wwhd/CameraTap.java"
  "${SOURCE}/runtime/tools/CombatMacroTest.java" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Combat macro test compilation failed")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}" org.wwhdrecomp.wwhd.CombatMacroTest RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Combat macro behavior test failed")
endif()
