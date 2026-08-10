SET(TARGET_H
    GLTFReader.h
    GLTFWriter.h
    B3DMReader.h
    B3DMWriter.h
)

SET(TARGET_SRC
    ReaderWriterGLTF.cpp
)

add_osgearth_plugin(
    TARGET osgdb_gltf
    HEADERS ${TARGET_H}
    SOURCES ${TARGET_SRC} )

find_package(meshoptimizer QUIET)
if(meshoptimizer_FOUND)
    target_link_libraries(osgdb_gltf PRIVATE meshoptimizer::meshoptimizer)
endif()
    
include_directories(
    ${OSGEARTH_EMBEDDED_THIRD_PARTY_DIR}/tinygltf 
    ${OSGEARTH_EMBEDDED_THIRD_PARTY_DIR}/rapidjson/include/rapidjson )

if (draco_FOUND)
    #include_directories(${draco_INCLUDE_DIRS})
    target_link_libraries(osgdb_gltf PRIVATE draco::draco)
endif()
