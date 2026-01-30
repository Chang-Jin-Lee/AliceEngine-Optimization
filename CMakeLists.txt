cmake_minimum_required(VERSION 3.20)

# ?ÑÎ°ú?ùÌä∏ ?¥Î¶Ñ?Ä ?ÑÏ≤¥ ?îÎ£®???¥Î¶Ñ
project(AliceRenderer LANGUAGES CXX)


# /utf-8 ?µÏÖò Ï∂îÍ? Í≤ΩÍ≥† ?àÎú®Í≤??òÍ∏∞ ?ÑÌï®
if(MSVC)
    add_compile_options(/utf-8)
endif()

# =========================================================================
# [?§Ï†ï] VCPKG Í≤ΩÎ°ú (Îß??ÑÏóê??Í¥ÄÎ¶?
# 1. "AUTO" : ?òÍ≤ΩÎ≥Ä??-> D?úÎùº?¥Î∏å -> C?úÎùº?¥Î∏å ?úÏÑúÎ°??êÎèô ?êÏÉâ
# 2. ÏßÅÏ†ë ÏßÄ??: "E:/MyLibs/vcpkg" Ï≤òÎüº Í≤ΩÎ°ú ?ÖÎ†•
# =========================================================================
set(ALICE_VCPKG_PATH_OPTION "AUTO")

# --- VCPKG Í≤ΩÎ°ú ?êÎèô Í∞êÏ? Î°úÏßÅ ---
if(ALICE_VCPKG_PATH_OPTION STREQUAL "AUTO")
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_DETECTED $ENV{VCPKG_ROOT})
        message(STATUS "[Alice] VCPKG: ?òÍ≤ΩÎ≥Ä?òÏóê??Í∞êÏ???-> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "D:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "D:/vcpkg")
        message(STATUS "[Alice] VCPKG: D?úÎùº?¥Î∏å?êÏÑú Í∞êÏ???-> ${VCPKG_ROOT_DETECTED}")
    elseif(EXISTS "C:/vcpkg/vcpkg.exe")
        set(VCPKG_ROOT_DETECTED "C:/vcpkg")
        message(STATUS "[Alice] VCPKG: C?úÎùº?¥Î∏å?êÏÑú Í∞êÏ???-> ${VCPKG_ROOT_DETECTED}")
    else()
        message(FATAL_ERROR "[Alice] vcpkgÎ•?Ï∞æÏùÑ ???ÜÏäµ?àÎã§! Î∞∞Ïπò ?åÏùº??Î®ºÏ? ?§Ìñâ?òÍ±∞??Í≤ΩÎ°úÎ•?ÏßÄ?ïÌï¥Ï£ºÏÑ∏??")
    endif()
else()
    set(VCPKG_ROOT_DETECTED ${ALICE_VCPKG_PATH_OPTION})
    message(STATUS "[Alice] VCPKG: ?¨Ïö©??ÏßÄ??Í≤ΩÎ°ú ?¨Ïö© -> ${VCPKG_ROOT_DETECTED}")
endif()

# Î≥Ä?òÎäî ?¨Í∏∞???ïÏ†ï??(Ï∫êÏãú???Ä?•Ìïò??GUI?êÏÑú??Î≥¥Ïù¥Í≤???
set(VCPKG_ROOT "${VCPKG_ROOT_DETECTED}" CACHE PATH "Path to vcpkg root" FORCE)
set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
set(VCPKG_TRIPLET_DYNAMIC "x64-windows" CACHE STRING "vcpkg dynamic triplet")


# ==========================================
# [?§Ï†ï] Í∏∞Î≥∏ Ïª¥Ìåå???µÏÖò
# ==========================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ï∂úÎ†• ?îÎ†â?∞Î¶¨ ?§Ï†ï (ÎπåÎìú ?¥Îçî ?¥Î? bin/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(ALICE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/src")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/ImGui")
set(IMGUIZMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/ImGuizmo")
#set(IMGUI_NODE_EDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/imgui-node-editor")
set(THIRDPARTY_3DMODEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/3DModel")
set(FMOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/FMOD")

# ==========================================
# [?§Ï†ï] RTTR ?ºÏù¥Î∏åÎü¨Î¶?(Engine???µÌï©)
# ==========================================
# Boost/Doxygen ?òÏ°¥?±ÏùÑ ?úÍ±∞?òÍ≥† Í∞ÄÎ≥çÍ≤å ÎπåÎìú?òÍ∏∞ ?ÑÌïú ?µÏÖò??
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# RTTR?Ä sharedÎ°?ÎπåÎìú?¥Ïïº Launch.exe, AliceScripts.dll ?¨Ïù¥?êÏÑú
# ?±Î°ù???ÑÎ°ú?ºÌã∞Í∞Ä Í∞ôÏ? registryÎ°??©Ï≥êÏßëÎãà??
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)

# ?∞Ì????ºÏù¥Î∏åÎü¨Î¶??§Ï†ï (vcpkg Î∞??îÏßÑÍ≥?ÎßûÏ∂îÍ∏??ÑÌï¥ Static Runtime ??-> MD/MDd ?¨Ïö©)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)

# RTTR º≠∫Í∏µ‚ √ﬂ∞° (∞Ê∑Œ: ThirdParty/rttr)
add_subdirectory(Engine/ThirdParty/rttr)

# window, std ?òÏùò min, max Ï∂©Îèå??†ú
add_compile_definitions(NOMINMAX)

# ==========================================
# [Target] Engine (Í∏∞Ï°¥ AliceEngine)
# ==========================================
set(ENGINE_SOURCES
    # ?îÏßÑ Í≥ÑÏ∏µ
    ${ALICE_SRC_DIR}/Engine/Engine.cpp
    ${ALICE_SRC_DIR}/Engine/EngineInitialize.cpp
    ${ALICE_SRC_DIR}/Engine/EngineUpdate.cpp
    ${ALICE_SRC_DIR}/Engine/EngineRender.cpp
    ${ALICE_SRC_DIR}/Engine/EnginePhysics.cpp
    ${ALICE_SRC_DIR}/Engine/EngineWindow.cpp
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.cpp
    ${ALICE_SRC_DIR}/Editor/EditorCore.cpp
    ${ALICE_SRC_DIR}/Editor/Blueprint/AnimBlueprintEditor.cpp

    # ÏΩîÏñ¥(ECS ?§Ì???
    ${ALICE_SRC_DIR}/Core/World.cpp
    ${ALICE_SRC_DIR}/Core/ThreadSafety.cpp
    ${ALICE_SRC_DIR}/Core/IScript.cpp
    ${ALICE_SRC_DIR}/Core/ScriptFactory.cpp
    ${ALICE_SRC_DIR}/Core/ScriptSystem.cpp
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.cpp
    ${ALICE_SRC_DIR}/Core/Prefab.cpp
    ${ALICE_SRC_DIR}/Core/Material.cpp
    ${ALICE_SRC_DIR}/Core/SceneFile.cpp
    ${ALICE_SRC_DIR}/Core/ComponentRegistry.cpp
    ${ALICE_SRC_DIR}/Core/EditorComponentRegistry.cpp
    ${ALICE_SRC_DIR}/Core/CameraSystem.cpp
    ${ALICE_SRC_DIR}/Core/ResourceManager.cpp
    ${ALICE_SRC_DIR}/Core/Scene.cpp
    ${ALICE_SRC_DIR}/Core/TimeSystem.cpp
    ${ALICE_SRC_DIR}/Core/InputSystem.cpp
    ${ALICE_SRC_DIR}/Core/Logger.cpp
    ${ALICE_SRC_DIR}/Core/Singleton.cpp
    ${ALICE_SRC_DIR}/Core/Helper.cpp
    ${ALICE_SRC_DIR}/Core/Vertex.cpp
    ${ALICE_SRC_DIR}/Core/Input.cpp
    ${ALICE_SRC_DIR}/Core/AdvancedAnimSystem.cpp

    # Audio
    ${ALICE_SRC_DIR}/Audio/AudioSystem.cpp
    ${ALICE_SRC_DIR}/Audio/SoundManager.cpp

    # FBX/3D Î™®Îç∏ Î°úÎçî
    ${ALICE_SRC_DIR}/3Dmodel/FbxMaterial.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxModel.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxSkeleton.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxGeometry.cpp
    ${ALICE_SRC_DIR}/3Dmodel/FbxAnimation.cpp

    # AnimBlueprint
    ${ALICE_SRC_DIR}/Game/AnimBlueprintAsset.cpp
    ${ALICE_SRC_DIR}/Game/SocketAttachmentSystem.cpp
    ${ALICE_SRC_DIR}/Game/SocketWorldUpdateSystem.cpp
    ${ALICE_SRC_DIR}/Game/HurtboxFactory.cpp
    ${ALICE_SRC_DIR}/Game/WeaponTraceSystem.cpp
    ${ALICE_SRC_DIR}/Game/CombatSystem.cpp
    ${ALICE_SRC_DIR}/Game/AttackDriverSystem.cpp

    # ?åÎçîÎß?Í≥ÑÏ∏µ
    ${ALICE_SRC_DIR}/Rendering/Camera.cpp
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.cpp
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/DeferredRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.cpp
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/PostProcessVolumeSystem.cpp
    
    #PhysX
    ${ALICE_SRC_DIR}/PhysX/PhysXContext.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Actors.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Core.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Joints.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Queries.cpp
    ${ALICE_SRC_DIR}/PhysX/PhysicsSystem.cpp
    #Physx Module
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsDebug.cpp
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsModule.cpp

    #UI
     ${ALICE_SRC_DIR}/UI/UIWorldManager.cpp
     ${ALICE_SRC_DIR}/UI/UISceneManager.cpp

     #UI Component
    ${ALICE_SRC_DIR}/UI/IUIComponent.cpp
    ${ALICE_SRC_DIR}/UI/UITransform.cpp
    ${ALICE_SRC_DIR}/UI/UI_ImageComponent.cpp
    
    ${ALICE_SRC_DIR}/UI/UI_ScriptComponent.cpp
    ${ALICE_SRC_DIR}/UI/IUIScript.cpp
    ${ALICE_SRC_DIR}/UI/UI_InputComponent.cpp
    
     #UI Object
     ${ALICE_SRC_DIR}/UI/UIBase.cpp
     ${ALICE_SRC_DIR}/UI/UIImage.cpp
     ${ALICE_SRC_DIR}/UI/UIButton.cpp
    
    #UI IUIScripts
    ${ALICE_SRC_DIR}/UI/UIScriptSystem.cpp
    ${ALICE_SRC_DIR}/UI/UIScriptFactory.cpp

    # ?êÏù¥??ÏΩîÎìú Î¨∂Ïùå
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/CommonShaderCode.cpp
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/DeferredShader.cpp
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/ForwardShader.cpp
    ${ALICE_SRC_DIR}/Rendering/EffectSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.cpp
    ${ALICE_SRC_DIR}/Rendering/ComputeEffectSystem.cpp
    ${ALICE_SRC_DIR}/Rendering/DebugDrawComponentSystem.cpp

    ${ALICE_SRC_DIR}/UI/UIGaugeBar.cpp

    # AliceUI (Shader ±‚π› UI)
    ${ALICE_SRC_DIR}/AliceUI/UIFont.cpp
    ${ALICE_SRC_DIR}/AliceUI/UIRenderer.cpp
    ${ALICE_SRC_DIR}/AliceUI/UIShaderCode.cpp
    ${ALICE_SRC_DIR}/AliceUI/UICurveAsset.cpp
)

set(ENGINE_HEADERS
    # ?îÏßÑ Í≥ÑÏ∏µ
    ${ALICE_SRC_DIR}/Engine/Engine.h
    ${ALICE_SRC_DIR}/Engine/EngineImpl.h
    ${ALICE_SRC_DIR}/Editor/ViewportPicker.h
    ${ALICE_SRC_DIR}/Editor/EditorCore.h
    ${ALICE_SRC_DIR}/Editor/Blueprint/AnimBlueprintEditor.h

    # ÏΩîÏñ¥
    ${ALICE_SRC_DIR}/Core/World.h
    ${ALICE_SRC_DIR}/Core/ThreadSafety.h
    ${ALICE_SRC_DIR}/Core/GameObject.h
    ${ALICE_SRC_DIR}/Core/Delegate.h
    ${ALICE_SRC_DIR}/Core/IScript.h
    ${ALICE_SRC_DIR}/Core/ScriptFactory.h
    ${ALICE_SRC_DIR}/Core/ScriptSystem.h
    ${ALICE_SRC_DIR}/Core/ScriptAPI.h
    ${ALICE_SRC_DIR}/Core/ScriptHotReload.h
    ${ALICE_SRC_DIR}/Core/Prefab.h
    ${ALICE_SRC_DIR}/Core/Material.h
    ${ALICE_SRC_DIR}/Core/Logger.h
    ${ALICE_SRC_DIR}/Core/CameraSystem.h
    ${ALICE_SRC_DIR}/Core/SceneFile.h
    ${ALICE_SRC_DIR}/Core/SceneFileHelper.h
    ${ALICE_SRC_DIR}/Core/ReflectionSerializer.h
    ${ALICE_SRC_DIR}/Core/ReflectionUI.h
    ${ALICE_SRC_DIR}/Core/ComponentRegistry.h
    ${ALICE_SRC_DIR}/Core/EditorComponentRegistry.h
    ${ALICE_SRC_DIR}/Core/EditorComponentRegistry.inl
    ${ALICE_SRC_DIR}/Core/Component.h
    ${ALICE_SRC_DIR}/Core/Entity.h
    ${ALICE_SRC_DIR}/Core/System.h
    ${ALICE_SRC_DIR}/Core/StringUtils.h
    ${ALICE_SRC_DIR}/Core/ResourceManager.h
    ${ALICE_SRC_DIR}/Core/Scene.h
    ${ALICE_SRC_DIR}/Core/TimeSystem.h
    ${ALICE_SRC_DIR}/Core/InputSystem.h
    ${ALICE_SRC_DIR}/Core/ImGuiEx.h
    ${ALICE_SRC_DIR}/Core/Input.h
    ${ALICE_SRC_DIR}/Core/Singleton.h
    ${ALICE_SRC_DIR}/Core/Helper.h
    ${ALICE_SRC_DIR}/Core/Vertex.h
    ${ALICE_SRC_DIR}/Core/AdvancedAnimSystem.h
    ${ALICE_SRC_DIR}/Core/JsonRttr.h
    ${ALICE_SRC_DIR}/Core/InputTypes.h

    # Audio
    ${ALICE_SRC_DIR}/Audio/AudioSystem.h
    ${ALICE_SRC_DIR}/Audio/SoundManager.h

    
    # ƒƒ∆˜≥Õ∆ÆµÈ
    ${ALICE_SRC_DIR}/Components/ComponentStorage.h
    ${ALICE_SRC_DIR}/Components/CameraBlendComponent.h
    ${ALICE_SRC_DIR}/Components/CameraComponent.h
    ${ALICE_SRC_DIR}/Components/CameraFollowComponent.h
    ${ALICE_SRC_DIR}/Components/CameraInputComponent.h
    ${ALICE_SRC_DIR}/Components/CameraLookAtComponent.h
    ${ALICE_SRC_DIR}/Components/CameraShakeComponent.h
    ${ALICE_SRC_DIR}/Components/CameraSpringArmComponent.h
    ${ALICE_SRC_DIR}/Components/AdvancedAnimationComponent.h
    ${ALICE_SRC_DIR}/Components/DebugDrawBoxComponent.h
    ${ALICE_SRC_DIR}/Components/AdvancedAnimator.h
    ${ALICE_SRC_DIR}/Components/SoundBoxComponent.h
    ${ALICE_SRC_DIR}/Components/AudioListenerComponent.h
    ${ALICE_SRC_DIR}/Components/AudioSourceComponent.h
    ${ALICE_SRC_DIR}/Components/MaterialComponent.h
    ${ALICE_SRC_DIR}/Components/PointLightComponent.h
    ${ALICE_SRC_DIR}/Components/RectLightComponent.h
    ${ALICE_SRC_DIR}/Components/ScriptComponent.h
    ${ALICE_SRC_DIR}/Components/SkinnedAnimationComponent.h
    ${ALICE_SRC_DIR}/Components/SkinnedMeshComponent.h
    ${ALICE_SRC_DIR}/Components/SpotLightComponent.h
    ${ALICE_SRC_DIR}/Components/TransformComponent.h
    ${ALICE_SRC_DIR}/Components/TrailEffectComponent.h
    ${ALICE_SRC_DIR}/Components/EffectComponent.h
    ${ALICE_SRC_DIR}/Components/SocketAttachmentComponent.h
    ${ALICE_SRC_DIR}/Components/SocketPoseOutputComponent.h
    ${ALICE_SRC_DIR}/Components/HurtboxComponent.h
    ${ALICE_SRC_DIR}/Components/WeaponTraceComponent.h
    ${ALICE_SRC_DIR}/Components/HealthComponent.h
    ${ALICE_SRC_DIR}/Components/AttackDriverComponent.h
    ${ALICE_SRC_DIR}/Components/PostProcessVolumeComponent.h

    # FBX/3D Î™®Îç∏ Î°úÎçî ?§Îçî
    ${ALICE_SRC_DIR}/3Dmodel/FbxMaterial.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxModel.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxSkeleton.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxTypes.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxGeometry.h
    ${ALICE_SRC_DIR}/3Dmodel/FbxAnimation.h

    # ?åÎçîÎß?Í≥ÑÏ∏µ
    ${ALICE_SRC_DIR}/Rendering/Camera.h
    ${ALICE_SRC_DIR}/Rendering/ForwardRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/DeferredRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/SkinnedMeshRegistry.h
    ${ALICE_SRC_DIR}/Rendering/IRenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/D3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/D3D11/ID3D11RenderDevice.h
    ${ALICE_SRC_DIR}/Rendering/DebugDrawSystem.h
    ${ALICE_SRC_DIR}/Rendering/DebugDrawComponentSystem.h
    ${ALICE_SRC_DIR}/Rendering/RenderTypes.h
    ${ALICE_SRC_DIR}/Rendering/TrailEffectRenderSystem.h
    ${ALICE_SRC_DIR}/Rendering/PostProcessSettings.h
    ${ALICE_SRC_DIR}/Rendering/PostProcessVolumeSystem.h
    
    # ?êÏù¥??ÏΩîÎìú Î¨∂Ïùå
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/CommonShaderCode.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/DeferredShader.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/ForwardShader.h
    ${ALICE_SRC_DIR}/Rendering/EffectSystem.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Game/AdvancedAnimSystem.h
    ${ALICE_SRC_DIR}/Game/SocketAttachmentSystem.h
    ${ALICE_SRC_DIR}/Game/SocketWorldUpdateSystem.h
    ${ALICE_SRC_DIR}/Game/HurtboxFactory.h
    ${ALICE_SRC_DIR}/Game/WeaponTraceSystem.h
    ${ALICE_SRC_DIR}/Game/CombatPhysicsLayers.h
    ${ALICE_SRC_DIR}/Game/CombatHitEvent.h
    ${ALICE_SRC_DIR}/Game/CombatSystem.h
    ${ALICE_SRC_DIR}/Game/AttackDriverSystem.h
    #PhysX Î¨ºÎ¶¨
    ${ALICE_SRC_DIR}/PhysX/IPhysicsWorld.h
    ${ALICE_SRC_DIR}/PhysX/PhysicsMath.h
    ${ALICE_SRC_DIR}/PhysX/PhysXContext.h
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld.h
    ${ALICE_SRC_DIR}/PhysX/PhysXWorld_Internal.h
    ${ALICE_SRC_DIR}/PhysX/PhysicsSystem.h

    #Module
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsDebug.h
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsEcsBridge.h
    ${ALICE_SRC_DIR}/PhysX/Module/PhysicsModule.h
    #Components
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_SettingsComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_ColliderComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_RigidBodyComponent.h    
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_TerrainHeightFieldComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_MeshColliderComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_JointComponent.h


     #UI
    ${ALICE_SRC_DIR}/UI/UIWorldManager.h
    ${ALICE_SRC_DIR}/UI/UISceneManager.h
    ${ALICE_SRC_DIR}/UI/UIRenderStruct.h
    ${ALICE_SRC_DIR}/UI/UITransform.h
    ${ALICE_SRC_DIR}/UI/UICompDelegate.h
    ${ALICE_SRC_DIR}/UI/IUIScript.h
    ${ALICE_SRC_DIR}/UI/UIScriptSystem.h

     #UI Component
    ${ALICE_SRC_DIR}/UI/IUIComponent.h

     #UI Object
    ${ALICE_SRC_DIR}/UI/UIBase.h

    # AliceUI (Shader ±‚π› UI)
    ${ALICE_SRC_DIR}/AliceUI/UICommon.h
    ${ALICE_SRC_DIR}/AliceUI/UIWidgetComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UITransformComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIImageComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UITextComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIButtonComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIGaugeComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIEffectComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIAnimationComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIShakeComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIHover3DComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UIVitalComponent.h
    ${ALICE_SRC_DIR}/AliceUI/UICurveAsset.h
    ${ALICE_SRC_DIR}/AliceUI/UIFont.h
    ${ALICE_SRC_DIR}/AliceUI/UIRenderer.h
    ${ALICE_SRC_DIR}/AliceUI/UIShaderCode.h
    ${ALICE_SRC_DIR}/AliceUI/BindWidget.h

    # json
    ${ALICE_SRC_DIR}/json/json.hpp
    ${ALICE_SRC_DIR}/json/json_fwd.hpp

    ${ALICE_SRC_DIR}/Rendering/EffectSystem.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/TrailEffectShader.h
    ${ALICE_SRC_DIR}/PhysX/Components/Phy_CCTComponent.h
    ${ALICE_SRC_DIR}/Components/ComputeEffectComponent.h
    ${ALICE_SRC_DIR}/Rendering/ComputeEffectSystem.h
    ${ALICE_SRC_DIR}/Rendering/ShaderCode/ComputeEffectShader.h
    ${ALICE_SRC_DIR}/Core/SocketSerialization.h


    ${ALICE_SRC_DIR}/UI/UI_ImageComponent.h
    ${ALICE_SRC_DIR}/UI/UIImage.h
    ${ALICE_SRC_DIR}/UI/UI_ScriptComponent.h
    ${ALICE_SRC_DIR}/UI/UI_InputComponent.h
    ${ALICE_SRC_DIR}/UI/UIButton.h

    ${ALICE_SRC_DIR}/UI/UIGaugeBar.h
)

add_library(Engine STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

# [?§Ï†ï] Engine??RTTR ?∞Í≤∞ (PUBLIC: ?îÏßÑ???∞Îäî ?ÑÎ°ú?ùÌä∏??RTTR ?¨Ïö© Í∞Ä??
target_link_libraries(Engine PUBLIC RTTR::Core)

if(MSVC)
    # ComponentRegistry.cpp??RTTR ?±Î°ù??Ïª§Ï†∏??objÍ∞Ä Ïª§Ïßê
    target_compile_options(Engine PRIVATE /bigobj)
endif()

source_group(TREE ${ALICE_SRC_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# ==========================================
# [ThirdParty] FMOD
# ==========================================
# FMOD Í≤ΩÎ°ú Î≥Ä???§Ï†ï (Í∏∞Ï°¥ FMOD_DIR ?¨Ïö©)
set(FMOD_INC_DIR "${FMOD_DIR}/inc")
set(FMOD_LIB_DIR "${FMOD_DIR}/64x")

# Engine??FMOD ?§Îçî ?¨Ìï® (PUBLIC: ?îÏßÑ???∞Îäî ?ÑÎ°ú?ùÌä∏??FMOD ?§Îçî ?¨Ïö© Í∞Ä??
if(EXISTS "${FMOD_INC_DIR}")
    target_include_directories(Engine PUBLIC "${FMOD_INC_DIR}")
endif()

# Engine??FMOD ?ºÏù¥Î∏åÎü¨Î¶?ÎßÅÌÅ¨ (Debug??fmodL_vc.lib, Release??fmod_vc.lib)
if(EXISTS "${FMOD_LIB_DIR}/fmod_vc.lib" AND EXISTS "${FMOD_LIB_DIR}/fmodL_vc.lib")
    target_link_libraries(Engine PUBLIC
        debug     "${FMOD_LIB_DIR}/fmodL_vc.lib"
        optimized "${FMOD_LIB_DIR}/fmod_vc.lib"
    )
endif()

# ==========================================
# [Target] Launch & AlicePlayer
# ==========================================
# Í≥µÌÜµ Í≤åÏûÑ ÏΩîÎìú
set(APP_COMMON_SOURCES
    ${ALICE_SRC_DIR}/Scenes/SampleScene.cpp
    ${ALICE_SRC_DIR}/Game/FbxImporter.cpp
    ${ALICE_SRC_DIR}/Game/FbxAsset.cpp
)

set(APP_HEADERS
    ${ALICE_SRC_DIR}/Scenes/SampleScene.h
    ${ALICE_SRC_DIR}/Game/FbxImporter.h
    ${ALICE_SRC_DIR}/Game/FbxAsset.h
    ${ALICE_SRC_DIR}/Game/SkinnedMeshSystem.h
    ${ALICE_SRC_DIR}/Core/AdvancedAnimSystem.h
)

# 1. Launch (?êÎîî????Î™®Îìú) ?îÌä∏Î¶??¨Ïù∏??
set(LAUNCH_MAIN
    ${ALICE_SRC_DIR}/Sandbox/Main.cpp
)

# 2. AlicePlayer (?úÏàò Í≤åÏûÑ Î™®Îìú) ?îÌä∏Î¶??¨Ïù∏??
set(PLAYER_MAIN
    ${ALICE_SRC_DIR}/Sandbox/GameMain.cpp
)

# [Target 1] Launch (Í∏∞Ï°¥ AliceGame/AliceRenderer)
add_executable(Launch
    ${LAUNCH_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

# [Target 2] AlicePlayer (ÏµúÏ¢Ö ÎπåÎìú???åÎ†à?¥Ïñ¥)
add_executable(AlicePlayer
    ${PLAYER_MAIN}
    ${APP_COMMON_SOURCES}
    ${APP_HEADERS}
)

source_group(TREE ${ALICE_SRC_DIR} FILES ${LAUNCH_MAIN} ${PLAYER_MAIN} ${APP_COMMON_SOURCES} ${APP_HEADERS})

# ==========================================
# [ThirdParty] ImGui
# ==========================================

# Engine??ImGui ?§ÎçîÎ•?Ï∞æÏùÑ ???àÎèÑÎ°??§Ï†ï
target_include_directories(Engine
    PRIVATE
        ${ALICE_SRC_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUI_NODE_EDITOR_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Engine/ThirdParty/rttr/src  # RTTR «Ï¥ı ∞Ê∑Œ
)


# ImGui ?ïÏ†Å ?ºÏù¥Î∏åÎü¨Î¶?
add_library(imgui STATIC
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.h
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.h
)

target_include_directories(imgui
    PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# Launch?Ä AlicePlayerÍ∞Ä ImGui ?§ÎçîÎ•?Ï∞æÏùÑ ???àÎèÑÎ°??§Ï†ï
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${ALICE_SRC_DIR}
            ${IMGUI_DIR}
            ${IMGUI_DIR}/backends
    )
    if(EXISTS "${FMOD_DIR}/inc")
        target_include_directories(${target} PRIVATE "${FMOD_DIR}/inc")
        target_link_directories(${target} PRIVATE "${FMOD_DIR}/64x")
    endif()
endforeach()

# ==========================================
# [ThirdParty] ImGuizmo
# ==========================================

# Engine??ImGuizmo ?§ÎçîÎ•?Ï∞æÏùÑ ???àÎèÑÎ°??§Ï†ï
target_include_directories(Engine
    PRIVATE
        ${IMGUIZMO_DIR}
)

# ImGuizmo ?ïÏ†Å ?ºÏù¥Î∏åÎü¨Î¶?
add_library(imguizmo STATIC
    ${IMGUIZMO_DIR}/ImGuizmo.cpp
    ${IMGUIZMO_DIR}/ImGuizmo.h
)

target_include_directories(imguizmo
    PUBLIC
        ${IMGUIZMO_DIR}
)

# ImGuizmoÎ•?DirectX Î™®ÎìúÎ°?Ïª¥Ìåå??(LH Ï¢åÌëúÍ≥? depth 0~1)
target_compile_definitions(imguizmo
    PUBLIC
        IMGUIZMO_USE_DIRECTX
)

# Engine???ôÏùº???ïÏùò ?¨Ïö©
target_compile_definitions(Engine
    PRIVATE
        IMGUIZMO_USE_DIRECTX
)

# ImGuizmo??ImGui???òÏ°¥
target_link_libraries(imguizmo
    PUBLIC
        imgui
)

# ?§Ìñâ ?åÏùº?§Ïù¥ ImGuizmo ?§ÎçîÎ•?Ï∞æÏùÑ ???àÎèÑÎ°??§Ï†ï
foreach(target Launch AlicePlayer)
    target_include_directories(${target}
        PRIVATE
            ${IMGUIZMO_DIR}
    )
endforeach()

# ==========================================
# [ThirdParty] ImGui Node Editor
# ==========================================

# Í≤ΩÎ°ú Î∞òÏòÅ
#set(IMGUI_NODE_EDITOR_INCLUDE "${IMGUI_NODE_EDITOR_DIR}")
#set(IMGUI_NODE_EDITOR_SOURCE  "${IMGUI_NODE_EDITOR_DIR}")
#
#if(NOT EXISTS "${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor.cpp")
#    message(FATAL_ERROR
#        "?§Î•ò: imgui-node-editor ?åÏä§Î•?Ï∞æÏùÑ ???ÜÏäµ?àÎã§!\n"
#        "Í≤ΩÎ°ú ?ïÏù∏: ${IMGUI_NODE_EDITOR_SOURCE}\n"
#        "cpp/h ?åÏùº?§Ïù¥ ?òÏúÑ ?¥Îçî ?ÜÏù¥ ?¥Îãπ ?ÑÏπò??Î∞îÎ°ú ?àÎäîÏßÄ ?ïÏù∏?¥Ï£º?∏Ïöî."
#    )
#endif()
#
#add_library(imgui_node_editor STATIC
#    ${IMGUI_NODE_EDITOR_SOURCE}/crude_json.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_canvas.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor.cpp
#    ${IMGUI_NODE_EDITOR_SOURCE}/imgui_node_editor_api.cpp
#)
#
#target_include_directories(imgui_node_editor PUBLIC
#    ${IMGUI_NODE_EDITOR_INCLUDE}
#)
#
#target_link_libraries(imgui_node_editor PUBLIC
#    imgui
#)
#
#target_compile_definitions(imgui_node_editor PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# Engine?êÏÑú ?∞ÎäîÍ≤?Î≥¥ÌÜµ?¥Îãà Engine?êÎßå Î¨ºÎ¶º(?§Ìñâ?åÏùº?êÎäî Ï§ëÎ≥µ ÎßÅÌÅ¨ X)
#target_link_libraries(Engine PUBLIC imgui_node_editor)

# ==========================================
# [?§Ï†ï] Ïª¥Ìåå???µÏÖò Î∞?vcpkg ?ÅÏö©
# ==========================================
if(MSVC)
    # Í≤ΩÍ≥† ?àÎ≤®
    target_compile_options(Engine      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(Launch      PRIVATE /W4 /permissive- /bigobj)
    target_compile_options(AlicePlayer PRIVATE /W4 /permissive- /bigobj)

    # Include Í≤ΩÎ°ú Ï∂îÍ? (?ÅÎã®?êÏÑú ?§Ï†ï??VCPKG_ROOT ?¨Ïö©)
    target_include_directories(Engine
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # Launch?Ä AlicePlayer??VCPKG Include/Lib Í≤ΩÎ°ú ?§Ï†ï
    foreach(target Launch AlicePlayer)
        target_include_directories(${target}
            PRIVATE
                "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
                "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
        )

        target_link_directories(${target}
            PRIVATE
                "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
                "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
                "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
                "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
        )
    endforeach()
endif()

# Win32 ?òÏúÑ ?úÏä§??(ÏΩòÏÜî ?®Í?)
set_target_properties(Launch PROPERTIES WIN32_EXECUTABLE YES)
set_target_properties(AlicePlayer PROPERTIES WIN32_EXECUTABLE YES)

# ==========================================
# [Link] ?ºÏù¥Î∏åÎü¨Î¶??∞Í≤∞
# ==========================================
# ???ÄÍ≤?Î™®Îëê ?ôÏùº???ºÏù¥Î∏åÎü¨Î¶??òÏ°¥?±ÏùÑ Í∞ÄÏß?
foreach(target Launch AlicePlayer)
    target_link_libraries(${target}
        PRIVATE
            Engine          # ?µÏã¨ ?îÏßÑ
            imgui
            imguizmo
            #imgui_node_editor
            d3d11
            dxgi
            dxguid
            d3dcompiler
            directxtk
            directxtex
            $<$<CONFIG:Debug>:assimp-vc143-mtd>
            $<$<CONFIG:Release>:assimp-vc143-mt>
    )
endforeach()

# ==========================================
# [Post-Build] DLL Î≥µÏÇ¨
# ==========================================

# Launch?Ä AlicePlayer ?ÄÍ≤?Î™®Îëê??DLL Î≥µÏÇ¨ ?òÌñâ
foreach(target Launch AlicePlayer)
    add_custom_command(TARGET ${target} POST_BUILD
        # RTTR shared ?§ÌÅ¨Î¶ΩÌä∏ DLLÍ≥?registry Í≥µÏú†
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rttr_core>" "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:rttr_core>"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/zlib1.dll"       "$<TARGET_FILE_DIR:${target}>/zlib1.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/zlibd1.dll"      "$<TARGET_FILE_DIR:${target}>/zlibd1.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/quazip.dll"      "$<TARGET_FILE_DIR:${target}>/quazip.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/pugixml.dll"     "$<TARGET_FILE_DIR:${target}>/pugixml.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/poly2tri.dll"    "$<TARGET_FILE_DIR:${target}>/poly2tri.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/kubazip.dll"      "$<TARGET_FILE_DIR:${target}>/kubazip.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRDPARTY_3DMODEL_DIR}/minizip.dll"      "$<TARGET_FILE_DIR:${target}>/minizip.dll"

        # vcpkg Assimp DLL Î≥µÏÇ¨
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin/assimp-vc143-mtd.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mtd.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin/assimp-vc143-mt.dll"
            "$<TARGET_FILE_DIR:${target}>/assimp-vc143-mt.dll"
    )
endforeach()

# ==========================================
# PhysX
# ==========================================
if(MSVC)

  foreach(t Launch AlicePlayer)
    target_link_options(${t} PRIVATE
      "$<$<CONFIG:Debug>:/NODEFAULTLIB:PhysXExtensions_static_64.lib>"
    )
  endforeach()
   
    # vcpkg toolchain¿ª æ» æ≤¥¬ ±∏º∫¿Ã∂Û∏È, find_package∞° √£¿ª ºˆ ¿÷∞‘ prefix∏∏ √ﬂ∞°
    list(PREPEND CMAKE_PREFIX_PATH
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}"
    )

    # vcpkg physx ?¨Ìä∏Í∞Ä ?¥Î≥¥?¥Îäî ?®ÌÇ§ÏßÄ/?ÄÍ≤?
    find_package(unofficial-omniverse-physx-sdk CONFIG REQUIRED)

    # ?îÏßÑ??PhysX ?∞Í≤∞ (Debug/Release ?¨Î∞îÎ•?lib Ï°∞Ìï©?Ä ?ÄÍ≤üÏù¥ ?åÏïÑ??Ï≤òÎ¶¨)
    target_link_libraries(Engine PUBLIC unofficial::omniverse-physx-sdk::sdk)

    # include Î≥¥Í∞ï
    target_include_directories(Engine PRIVATE
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
        "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include/physx"
    )

    # PhysX ?∞Ì???DLL Î≥µÏÇ¨ (?ÑÏöî??Í≤ÉÎßå ÏµúÏÜåÎ°?
    set(PHYSX_BINDIR_DEBUG "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/bin")
    set(PHYSX_BINDIR_REL   "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/bin")

    foreach(target Launch AlicePlayer)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXFoundation_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXFoundation_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXFoundation_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXCommon_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXCommon_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXCommon_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysX_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysX_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysX_64.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${PHYSX_BINDIR_DEBUG}/PhysXCooking_64.dll>$<$<NOT:$<CONFIG:Debug>>:${PHYSX_BINDIR_REL}/PhysXCooking_64.dll>"
                "$<TARGET_FILE_DIR:${target}>/PhysXCooking_64.dll"
        )
    endforeach()
endif()



# ==========================================
# [Post-Build] FMOD DLL Î≥µÏÇ¨
# ==========================================
if(WIN32)
    if(NOT EXISTS "${FMOD_LIB_DIR}/fmod.dll")
        message(FATAL_ERROR "[FMOD] not found: ${FMOD_LIB_DIR}/fmod.dll")
    endif()
    if(NOT EXISTS "${FMOD_LIB_DIR}/fmodL.dll")
        message(FATAL_ERROR "[FMOD] not found: ${FMOD_LIB_DIR}/fmodL.dll")
    endif()

    # Launch?Ä AlicePlayer ?ÄÍ≤üÏóê DLL Î≥µÏÇ¨
    foreach(t Launch AlicePlayer)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FMOD_LIB_DIR}/fmod.dll"
                "$<TARGET_FILE_DIR:${t}>/fmod.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FMOD_LIB_DIR}/fmodL.dll"
                "$<TARGET_FILE_DIR:${t}>/fmodL.dll"
            VERBATIM
        )
    endforeach()
endif()