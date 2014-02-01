// Copyright (c) Jonathan Karlsson 2011-2012
// Code may be used freely for commercial and non-commercial purposes.
// Author retains his moral rights under the applicable copyright laws
// (i.e. credit the author where credit is due).
//

#include <vector>
#include <cstdlib>
#include <cmath>
#include "WavefrontOBJ.h"

OBJ::Material::Material( void )
{
	name = "default";
	for (int i = 0; i < 3; ++i) {
		ambient[i] = 0.2f;
		diffuse[i] = 0.8f;
		specular[i] = 1.f;
		emissive[i] = 0.f;
		transmission[i] = 1.f; // not sure if correct
	}
	alpha = 1.0f;
	dissolve = 1.0f; // NOTE: according to some sources Tr and d are the same thing
	shininess = 0.0f;
	opticalDensity = 10.0f; // not sure if correct
	sharpness = 60;
	illumination = Material::DIFFUSE;
	// map_Kx, disp, decal & bump have no defaults
}

OBJ::OBJ( void )
{}

bool OBJ::Open(File &file, const std::string &filename)
{
	file.fin.open(filename.c_str());
	if (file.fin.is_open()) {
		file.name = filename;
		file.lineNo = 0;
		return true;
	}
	warnings.push_back("Could not open \"" + filename + "\"");
	return false;
}

void OBJ::ReadLine(File &file) const
{
	file.type.clear();
	file.params.clear();

	std::string line;
	std::getline(file.fin, line);
	++file.lineNo;
	std::istringstream sin(line);
	sin >> file.type;
	std::getline(sin, file.params);
}

void OBJ::AddError(const File &file, std::ostringstream &sout)
{
	std::string errorInfo = sout.str();
	sout.str("");
	sout << file.name << ": Line " << file.lineNo << ": ";
	errors.push_back(sout.str() + errorInfo);
	sout.str("");
}

void OBJ::AddWarning(const File &file, std::ostringstream &sout)
{
	std::string errorInfo = sout.str();
	sout.str("");
	sout << file.name << ": Line " << file.lineNo << ": ";
	warnings.push_back(sout.str() + errorInfo);
	sout.str("");
}

// http://paulbourke.net/dataformats/obj/
// http://www.fileformat.info/format/material/
// To do:
// No full support for .MTL files. See documentation.
// HOW ARE MATERIALS ASSIGNED TO A SINGLE VERTEX INSTEAD OF A FACE? (c_interp)
// Add support for line continuation using "\" as token.
// Error handling for "name", "g" and "shadowModel" (shadowModel may only take one file)
// BUG: "mtllib" and "map_Ka" "shadowModel" do not handle paths with spaces properly. Add support for "-token.
// Remove the possibility to input several filenames in mtllib, map_Ka et al. Not necessary.
// For every LOD all materials need to be reread and restored, even if it has it in common with other LOD:s
OBJ::OBJ(const std::string &filename) :
	fileName(filename),
	name(),
	shadowModel(),
	levelOfDetail(),
	materials()
{
	static const int OBJ_NUM_KEYWORDS = 37;
	static const std::string OBJ_KEYWORDS[OBJ_NUM_KEYWORDS] = {
		"v", // supported
		"vt", // supported
		"vn", // supported
		"f", // supported
		"o", // supported
		"vp",
		"deg", // implement?
		"bmat", // implement?
		"step",
		"cstype",
		"p",
		"l",
		"curv",
		"curv2",
		"surf",
		"parm",
		"trim",
		"hole",
		"scrv",
		"sp",
		"end",
		"con",
		"g", // supported
		"s",
		"mg",
		"bevel",
		"c_interp",
		"d_interp",
		"lod", // supported
		"usemtl", // supported
		"mtllib", // supported
		"shadow_obj", // supported
		"trace_obj",
		"ctech",
		"stech",
		"maplib",
		"usemap"
	};

	// generate the working directory so that calls to 'mtllib' can be relative to the .obj file instead of the executable.
	size_t lastDirectory = std::string::npos;
	const size_t lastForwardSlash = filename.find_last_of('/');
	if (lastForwardSlash != std::string::npos) { lastDirectory = lastForwardSlash; }
	const size_t lastBackslash = filename.find_last_of('\\');
	if (lastBackslash != std::string::npos) {
		if (lastForwardSlash == std::string::npos) {
			lastDirectory = lastForwardSlash;
		} else {
			lastDirectory = (lastForwardSlash > lastBackslash) ? lastForwardSlash : lastBackslash;
		}
	}
	std::string workingDirectory = "";
	if (lastDirectory != std::string::npos) {
		workingDirectory = filename.substr(0, lastDirectory + 1);
	}

	StateVariables state;
	levelOfDetail.push_back(OBJ::LevelOfDetail());
	state.LOD = levelOfDetail.begin();
	state.LOD->groups.push_back(OBJ::Group());
	state.groups.push_back(state.LOD->groups.begin());
	materials.push_back(OBJ::Material()); // a default material
	state.material = materials.begin();
	
	std::ostringstream sout; // for concatenating error/warning strings
	File objFile; // handles the input stream from the file

	if (Open(objFile, filename)) {

		fileName = filename;

		while (!objFile.fin.eof()) {

			ReadLine(objFile);

			if (objFile.type == "o") {
				// read object name
				// must be a name without spaces
				name = objFile.params; // read this straight to the main object
			} else if (objFile.type == "v") {
				// read vertex position
				// fourth parameter is optional
				state.LOD->vertices.push_back(float4());
				ReadParams(objFile, 3, 4, 1.0f, (float*)state.LOD->vertices.back());
			} else if (objFile.type == "vt") {
				// read texture coordinates
				// second and third parameters are optional
				state.LOD->texCoords.push_back(float3());
				ReadParams(objFile, 1, 3, 0.0f, (float*)state.LOD->texCoords.back());
			} else if (objFile.type == "vn") {
				// read vertex normals
				// no optional parameters
				// normals need not be of unit length
				state.LOD->normals.push_back(float3());
				ReadParams(objFile, 3, (float*)state.LOD->normals.back());
			} else if (objFile.type == "f") {
				// read face definitions
				// face definitions can contain any number of vertex indices
				// indices are numbered 1 - n, not 0 - n-1, but are converted to 0 - n-1 (where -1 means "no index")
				// for simplicity; store faces > 3 as a fan of triangles
				std::list<std::string> vert;
				ReadVariableParams(objFile, Step_f_idx_elem, vert);
				std::vector<int> face; // intermediate for storing the current face
				for (std::list<std::string>::const_iterator vertex = vert.begin(); vertex != vert.end(); ++vertex) {
					size_t currentPos = 0;
					int i;
					for (i = 0; i < Step_f_idx_elem && currentPos != std::string::npos; ++i) { // parse v, v/vt, v/vt/vn, v//vn
						size_t searchFrom = currentPos + (i != 0);
						face.push_back( atoi(vertex->substr(searchFrom, vertex->find("/", searchFrom) - searchFrom).c_str()) - 1 );
						currentPos = vertex->find("/", searchFrom);
					}
					switch (i) { // adds missing elements if they where omitted from the .obj file (-1 is invalid value)
						case 1:
							face.push_back(-1);
						case 2:
							face.push_back(-1);
					};
					
					if (face[face.size()-3] < -1) { // < -1 indicates relative indexing (< -2 is represented < -1 in the file)
						const int relative = face[face.size()-3]+1;
						const int size = (int)state.LOD->vertices.size();
						const int absolute = size + relative;
						if (absolute < 0) {
							sout << "Relative index " << relative << " is out of defined range for \'v\' (size is " << size << ")";
							AddError(objFile, sout);
						} else {
							face[face.size()-3] = absolute;
						}
					}
					if (face[face.size()-2] < -1) {
						const int relative = face[face.size()-2]+1;
						const int size = (int)state.LOD->texCoords.size();
						const int absolute = size + relative;
						if (absolute < 0) {
							sout << "Relative index " << relative << " is out of defined range for \'vt\' (size is " << size << ")";
							AddError(objFile, sout);
						} else {
							face[face.size()-2] = absolute;
						}
					}
					if (face[face.size()-1] < -1) {
						const int relative = face[face.size()-1]+1;
						const int size = (int)state.LOD->normals.size();
						const int absolute = size + relative;
						if (absolute < 0) {
							sout << "Relative index " << relative << " is out of defined range for \'vn\' (size is " << size << ")";
							AddError(objFile, sout);
						} else {
							face[face.size()-1] = absolute;
						}
					}
					
					
					if (currentPos != std::string::npos) { // if this is true, then the parsing loop has broken at 3, yet there was more info to parse, meaning the .obj file is syntactically wrong.
						sout << "Syntax error (f v, f v/vt, f v/vt/vn, f v//vn)";
						AddError(objFile, sout);
					}
					if (face[face.size()-3] >= ((int)state.LOD->vertices.size())) {
						sout << "Index " << face[face.size()-3]+1 << " is out of defined range for \'v\'";
						AddError(objFile, sout);
					}
					if (face[face.size()-2] >= ((int)state.LOD->texCoords.size())) {
						sout << "Index " << face[face.size()-2]+1 << " is out of defined range for \'vt\'";
						AddError(objFile, sout);
					}
					if (face[face.size()-1] >= ((int)state.LOD->normals.size())) {
						sout << "Index " << face[face.size()-1]+1 << " is out of defined range for \'vn\'";
						AddError(objFile, sout);
					}
				}
				if (face.size()%Step_f_idx_elem != 0) { // sanity check, makes sure that every vertex index has three elements (v/vn/vt)
					sout << "Parsing code bug";
					AddError(objFile, sout);
				} else if (face.size() >= Step_f) {
					int numUnavailable = 0;
					size_t i;
					for (i = 0; i < Step_f_idx; ++i) {
						for (size_t j = i; j < face.size(); j+=Step_f_idx) { // count the number of omitted elements in the vertex index...
							if (face[j] == -1) { ++numUnavailable; }
						}
						if (numUnavailable % (face.size()/Step_f_idx) != 0) { // ...must be a multiple of the number of specified vertex indices
							// remember, this is /before/ the face definition is converted to a set of triangles, so omitted elements can be a non-multiple of 3 and still be valid.
							sout << "Vertex index mismatch";
							AddError(objFile, sout);
							break;
						}
					}
					if (i == Step_f_idx) { // or else error occurred
						// convert the face into a triangle fan
						// NOTE: if triangles are facing the wrong way, swap the order the elements are pushed
						for (size_t i=(size_t)Step_f_idx; i < face.size()-(size_t)Step_f_idx; i+=Step_f_idx) { // numParam has guaranteed that face.size() is at least 3
							
							state.LOD->facets.push_back(OBJ::Facet());
							OBJ::Facet &facet = state.LOD->facets.back();
							// vertex 1
							facet.vertex[0] = face[IndexPos];
							facet.texCoord[0] = face[IndexTex];
							facet.normal[0] = face[IndexNor];
							// vertex 2
							facet.vertex[1] = face[i+IndexPos];
							facet.texCoord[1] = face[i+IndexTex];
							facet.normal[1] = face[i+IndexNor];
							//vertex 3
							facet.vertex[2] = face[i+Step_f_idx+IndexPos];
							facet.texCoord[2] = face[i+Step_f_idx+IndexTex];
							facet.normal[2] = face[i+Step_f_idx+IndexNor];
							// material
							facet.material = state.materialIndex;
							// group
							for (std::list<GroupList::iterator>::iterator it = state.groups.begin(); it != state.groups.end(); ++it) {
								(*it)->facets.push_back(state.LOD->facets.size() - 1);
							}
						}
					}
				}
			} else if (objFile.type == "g") { // faces can belong to multiple groups
				
				// read parameters
				std::list<std::string> groupNames;
				ReadVariableParams(objFile, 1, groupNames);
				
				// find groups and construct a group list that all succeeding facets are part of
				state.groups.clear();
				for (std::list<std::string>::iterator gn = groupNames.begin(); gn != groupNames.end(); ++gn) {
					GroupList::iterator gi;
					for (gi = state.LOD->groups.begin(); gi != state.LOD->groups.end(); ++gi) {
						if (gi->name == *gn) { // the group already exists
							state.groups.push_back(gi);
							break;
						}
					}
					if (gi == state.LOD->groups.end()) { // the group does not exist, create new group
						Group newGroup;
						newGroup.name = *gn;
						state.LOD->groups.push_back(newGroup);
						state.groups.push_back(--gi);
					}
				}
			} else if (objFile.type == "usemtl") {
				MaterialList::const_iterator material = materials.begin();
				for (int i = 0; material != materials.end(); ++i, ++material) {
					if (material->name == objFile.params) {
						state.materialIndex = i;
						break;
					}
				}
				if (material == materials.end()) {
					sout << "Material \"" << objFile.params << "\" not defined";
					AddError(objFile, sout);
					state.materialIndex = OBJ::Facet::DEFAULT_MATERIAL;
				}
			}  else if (objFile.type == "mtllib") {
				File mtlFile;

				//
				// NOTE
				//
				// Each mtllib statement can contain
				// more than one filename. This is
				// currently not supported.
				//

				if (Open(mtlFile, workingDirectory + objFile.params)) {
					//
					// Note
					//
					// All of the keywords are supported,
					// albeit not fully. Keywords within
					// the keywords are not supported at
					// all.
					//
					static const int MTL_NUM_KEYWORDS = 20;
					static const std::string MTL_KEYWORDS[MTL_NUM_KEYWORDS] = {
						"newmtl", // supported
						"Ka", // supported
						"Kd", // supported
						"Ks", // supported
						"Ke", // supported
						"Tr", // supported
						"d", // supported
						"Tf", // supported
						"Ns", // supported
						"Ni", // supported
						"sharpness", // supported
						"illum", // supported
						"map_Ka", // supported
						"map_Kd", // supported
						"map_Ks", // supported
						"map_Ke", // supported
						"map_Tf", // supported
						"disp", // supported
						"decal", // supported
						"bump" // supported
					};
					
					state.material = materials.end();
					state.materialIndex = materials.size() - 1;

					while (!mtlFile.fin.eof()) {
						ReadLine(mtlFile);

						if (mtlFile.type == "newmtl") {
							std::string materialName;
							if (!mtlFile.params.empty()) {
								materialName = mtlFile.params;
							}

							if (materialName.find(" ") != std::string::npos || materialName.find("\t") != std::string::npos) {
								sout << "Material name may not include blank characters: see \"" << materialName << "\"";
								AddError(mtlFile, sout);
								state.material = materials.end(); // if material name failed mtl is set to invalid value
								state.materialIndex = -1;
							} else { // name is OK
								for (state.material = materials.begin(); state.material != materials.end(); ++state.material) {
									if (materialName == state.material->name) {
										break;
									}
								}
								if (state.material == materials.end()) { // if you get here, then material name passed all error checks
									materials.push_back(OBJ::Material()); // automatically sets up defaults
									state.material = materials.end();
									--state.material;
									state.materialIndex = materials.size() - 1;
									state.material->name = materialName;
								} else {
									sout << "Redefinition of material \"" << state.material->name << "\"";
									AddError(mtlFile, sout);
									state.material = materials.end(); // set mtl to invalid value
									state.materialIndex = -1;
								}
							}
						} else if (state.material != materials.end()) {
							//
							// Note
							//
							// Ka, Kd, Ks et al. are not implemented correctly.
							// Read their values as strings, not as floats, since
							// parameters can contain keywords such as "spectral".
							//
							if (mtlFile.type == "Ka") { // ambient color
								ReadParams(mtlFile, 3, (float*)state.material->ambient);
							} else if (mtlFile.type == "Kd") { // diffuse color
								ReadParams(mtlFile, 3, (float*)state.material->diffuse);
							} else if (mtlFile.type == "Ks") { // specular color
								ReadParams(mtlFile, 3, (float*)state.material->specular);
							} else if (mtlFile.type == "Ke") { // emissive color
								ReadParams(mtlFile, 3, (float*)state.material->emissive);
							} else if (mtlFile.type == "Tr") { // alpha
								ReadParams(mtlFile, 1, &state.material->alpha);
							} else if (mtlFile.type == "d") { // dissolve (same as alpha?)
								ReadParams(mtlFile, 1, &state.material->dissolve);
							} else if (mtlFile.type == "Tf") { // transmission filter
								ReadParams(mtlFile, 3, (float*)state.material->transmission);
							} else if (mtlFile.type == "Ns") { // shininess
								ReadParams(mtlFile, 1, &state.material->shininess);
							} else if (mtlFile.type == "Ni") { // optical density
								ReadParams(mtlFile, 1, &state.material->opticalDensity);
							} else if (mtlFile.type == "sharpness") { // sharpness
								ReadParams(mtlFile, 1, &state.material->sharpness);
							} else if (mtlFile.type == "illum") { // illumination
								ReadParams(mtlFile, 1, &state.material->illumination);
								int illum = state.material->illumination;
								if (illum != OBJ::Material::FLAT && illum != OBJ::Material::DIFFUSE && illum != OBJ::Material::DIFFUSE_AND_SPECULAR) {
									sout << "\'" << mtlFile.type << "\' is not set to a recognisable shader model (only flat (0), diffuse (1), diffuse + specular (2)).";
									AddWarning(mtlFile, sout);
								}
							}
							//
							// NOTE
							//
							// map_Kx can contain more information than just
							// a file name. This is currently not supported.
							//
							else if (mtlFile.type == "map_Ka") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->ambientMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Kd") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->diffuseMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Ks") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->specularMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Ke") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->emissiveMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Tf") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->transmissionMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Ns") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->shininessMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_Tr") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->alphaMap = mapFile.name;
								}
							} else if (mtlFile.type == "map_d") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->dissolveMap = mapFile.name;
								}
							} else if (mtlFile.type == "disp") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->displacementMap = mapFile.name;
								}
							} else if (mtlFile.type == "decal") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->detailMap = mapFile.name;
								}
							} else if (mtlFile.type == "bump") {
								File mapFile;
								if (Open(mapFile, workingDirectory + mtlFile.params)) {
									state.material->bumpMap = mapFile.name;
								}
							} else if (mtlFile.type.size() > 0 && mtlFile.type[0] != '#') {
								int i = 0;
								for (; i < MTL_NUM_KEYWORDS; ++i) {
									if (mtlFile.type == MTL_KEYWORDS[i]) { break; }
								}
								if (i < MTL_NUM_KEYWORDS) { // output warning if keyword is valid, but not supported
									sout << " \'" << MTL_KEYWORDS[i] << "\' is not supported at this time";
									AddWarning(mtlFile, sout);
								} else {
									sout << " Unknown type \'" << mtlFile.type << "\'";
									AddError(mtlFile, sout);
								}
							}
						} else if (mtlFile.type.size() > 0 && mtlFile.type[0] != '#') {
							int i = 0;
							for (; i < MTL_NUM_KEYWORDS; ++i) {
								if (mtlFile.type == MTL_KEYWORDS[i]) { break; }
							}
							if (i < MTL_NUM_KEYWORDS) { // output warning if keyword is valid, but not supported
								sout << "\'" << mtlFile.type << "\' operating on undefined material";
								AddError(mtlFile, sout);
							} else {
								sout << " Unknown type \'" << mtlFile.type << "\'";
								AddError(mtlFile, sout);
							}
						}
					}

				} else {
					sout << "Specified files could not be opened";
					AddError(objFile, sout);
				}
			} else if (objFile.type == "shadow_obj") {
				// According to the standard, there can be only one
				// shadow object per .obj file (not one for each LOD).
				// Only the last specified shadow_obj filename is relevant.
				shadowModel = objFile.params;
			} else if (objFile.type == "lod") {
				int lodVal;
				ReadParams(objFile, 1, &lodVal);
				if (state.LOD->facets.size() == 0) { // LOD does not contain any relevant data
					sout << "Previous LOD " << state.LOD->levelOfDetail << " does not contain any relevant data. Skipping...";
					AddWarning(objFile, sout);
					levelOfDetail.erase(state.LOD);
				}
				for (state.LOD = levelOfDetail.begin(); state.LOD != levelOfDetail.end(); ++state.LOD) {
					if (lodVal >= state.LOD->levelOfDetail) {
						break;
					}
				}
				state.LOD = levelOfDetail.insert(state.LOD, OBJ::LevelOfDetail());
			} else if (!objFile.type.empty() && objFile.type[0] != '#') {
				int i = 0;
				for (; i < OBJ_NUM_KEYWORDS; ++i) {
					if (objFile.type == OBJ_KEYWORDS[i]) { break; }
				}
				if (i < OBJ_NUM_KEYWORDS) { // output warning if keyword is valid, but not supported
					sout << " \'" << OBJ_KEYWORDS[i] << "\' is not supported at this time";
					AddWarning(objFile, sout);
				} else {
					sout << " Unknown type \'" << objFile.type << "\'";
					AddError(objFile, sout);
				}
			}
		}
		if (state.LOD->facets.size() == 0) {
			sout << "File does not contain any face definitions";
			warnings.push_back(sout.str());
			sout.str("");
		}
	} else {
		sout << "\"" << objFile.name << "\": File could not be opened";
		errors.push_back(sout.str());
		sout.str("");
	}
}

OBJ::Status OBJ::GetStatus( void ) const
{
	if (!errors.empty()) {
		return OBJ::ERRORS;
	} else if (!warnings.empty()) {
		return OBJ::WARNINGS;
	}
	return OBJ::OK;
}

void OBJ::Reverse( void )
{
	// models are made for looking down the negative z axis
	// engine looks down the positive z axis
	// 1. reverse triangle winding order
	// 2. negate model's z coordinates (will this muck with winding order, i.e. do I need to change BOTH winding order and z coordinates - if no, change z coordinates)
	// 3. negate model's normals' z coordinates
	for (LODList::iterator lod = levelOfDetail.begin(); lod != levelOfDetail.end(); ++lod) {
		// Swap triangle winding order
		for (FacetList::iterator facet = lod->facets.begin(); facet != lod->facets.end(); ++facet) {
			Swap(facet->vertex[0], facet->vertex[2]);
			Swap(facet->texCoord[0], facet->texCoord[2]);
			Swap(facet->normal[0], facet->normal[2]);
		}
		// Invert Z axis
		for (VertexList::iterator vertex = lod->vertices.begin(); vertex != lod->vertices.end(); ++vertex) {
			(*vertex)[Z] = -(*vertex)[Z];
		}
		// Invert normals
		for (NormalList::iterator normal = lod->normals.begin(); normal != lod->normals.end(); ++normal) {
			(*normal)[X] = -(*normal)[X];
			(*normal)[Y] = -(*normal)[Y];
			(*normal)[Z] = -(*normal)[Z];
		}
	}
}

void OBJ::DumpErrors(std::ostream &out, const unsigned int MaxErrors) const
{
	size_t n = 0;
	for (std::list<std::string>::const_iterator i = errors.begin(); i != errors.end(); ++i){
		out << *i << std::endl;
		if (++n == MaxErrors) {
			out << "<< " << errors.size() - n << " more error(s) >>" << std::endl;
			break;
		}
	}
	out << "--" << errors.size() << " error(s)--" << std::endl;
}

void OBJ::DumpWarnings(std::ostream &out, const unsigned int MaxWarnings) const
{
	size_t n = 0;
	for (std::list<std::string>::const_iterator i = warnings.begin(); i != warnings.end(); ++i){
		out << *i << std::endl;
		if (++n == MaxWarnings) {
			out << "<< " << warnings.size() - n << " more warning(s) >>" << std::endl;
			break;
		}
	}
	out << "--" << warnings.size() << " warning(s)--" << std::endl;
}
