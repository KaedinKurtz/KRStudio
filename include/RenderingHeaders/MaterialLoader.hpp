// MaterialLoader.hpp
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "Texture2D.hpp"
#include "components.hpp"  // for MaterialComponent

/// Attempts to load a PBR material from `dirPath`.  
/// Expects files named (with any of these extensions):  
///   albedo.*      (base color)  
///   normal.*      (tangent-space normal)  
///   roughness.*   (roughness)  
///   metalness.*   (metallic)  
///   ao.*          (ambient occlusion)  
///   height.*      (height/displacement)  
/// Returns a MaterialComponent with each map pointer set (or nullptr),  
/// and scalar defaults left at their default values in MaterialComponent.
MaterialComponent loadMaterialFromDirectory(const std::string& dirPath);
