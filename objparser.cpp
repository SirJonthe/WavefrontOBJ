// Copyright (c) Jonathan Karlsson 2011
// Code may be used freely for commercial and non-commercial purposes.
// Author retains his moral rights under the applicable copyright laws
// (i.e. credit the author where credit is due).
//

#include <vector>
#include <cstdlib>
#include <cmath>
#include "objparser.h"

#include <iostream>

MTL::MTL( void )
{
	newmtl = "default";
	for (int i = 0; i < 3; ++i) {
		Ka[i] = 0.2f;
		Kd[i] = 0.8f;
		Ks[i] = 1.0f;
		Ke[i] = 0.0f;
		Tf[i] = 1.0f; // not sure if correct
	}
	Tr = 1.0f;
	d = 1.0f; // NOTE: according to some sources Tr and d are the same thing
	Ns = 0.0f;
	Ni = 10.0f; // not sure if correct
	sharpness = 60;
	illum = 1;
	// map_Kx, disp, decal & bump has no defaults
}

OBJ::OBJ( void ) : 
	file(), o(), v(NULL), vt(NULL), vn(NULL), newmtl(NULL), f(NULL), usemtl(NULL), g(NULL), shadow_obj(), lod(NULL), num_v(0), num_vt(0), num_vn(0), num_f(0), num_usemtl(0), num_g(0), num_newmtl(0)
{
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

void OBJ::Free(OBJ *LOD)
{
	if (LOD->lod != NULL) { Free(LOD->lod); } // recursive
	
	LOD->file.clear();
	LOD->o.clear();
	LOD->shadow_obj.clear();

	delete [] LOD->v;
	delete [] LOD->vt;
	delete [] LOD->vn;
	delete [] LOD->newmtl;
	delete [] LOD->f;
	delete [] LOD->usemtl;
	delete [] LOD->g;
	delete LOD->lod;

	LOD->v = NULL;
	LOD->vt = NULL;
	LOD->vn = NULL;
	LOD->newmtl = NULL;
	LOD->f = NULL;
	LOD->usemtl = NULL;
	LOD->g = NULL;
	LOD->lod = NULL;
	
	LOD->num_v = 0;
	LOD->num_vt = 0;
	LOD->num_vn = 0;
	LOD->num_newmtl = 0;
	LOD->num_f = 0;
	LOD->num_usemtl = 0;
	LOD->num_g = 0;
}

// http://paulbourke.net/dataformats/obj/
// http://www.fileformat.info/format/material/
// To do:
// No full support for .MTL files. See documentation.
// HOW ARE MATERIALS ASSIGNED TO A SINGLE VERTEX INSTEAD OF A FACE? (c_interp)
// Add support for line continuation using "\" as token.
// Error handling for "o", "g" and "shadow_obj" (shadow_obj may only take one file)
// BUG: "mtllib" and "map_Ka" "shadow_obj" do not handle paths with spaces properly. Add support for "-token.
// Remove the possibility to input several filenames in mtllib, map_Ka et al. Not necessary.
// For every LOD all materials need to be reread and restored, even if it has it in common with other LOD:s
OBJ::OBJ(const std::string &filename) :
	file(filename), o(), v(NULL), vt(NULL), vn(NULL), newmtl(NULL), f(NULL), usemtl(NULL), g(NULL), shadow_obj(), lod(NULL), num_v(0), num_vt(0), num_vn(0), num_f(0), num_usemtl(0), num_g(0), num_newmtl(0)
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
	
	std::list<OBJ::ObjData> lodData;
	ObjData firstLod;
	lodData.push_back(firstLod);
	std::list<OBJ::ObjData>::iterator currentLod = lodData.begin();

	std::ostringstream sout; // for concatenating error/warning strings
	File objFile; // handles the input stream from the file

	objFile.name = filename;
	objFile.lineNo = 0;
	objFile.fin.open(objFile.name.c_str());
	if (objFile.fin.is_open()) {
		while (!objFile.fin.eof()) {

			ReadLine(objFile);

			if (objFile.type == "o") {
				// read object name
				// must be a name without spaces
				o = objFile.params; // read this straight to the main object
			} else if (objFile.type == "v") {
				// read vertex position
				// fourth parameter is optional
				ReadParams(objFile, Step_v-1, Step_v, 1.0f, currentLod->v);
			} else if (objFile.type == "vt") {
				// read texture coordinates
				// second and third parameters are optional
				ReadParams(objFile, Step_vt-2, Step_vt, 0.0f, currentLod->vt);
			} else if (objFile.type == "vn") {
				// read vertex normals
				// no optional parameters
				// normals need not be of unit length
				ReadParams(objFile, Step_vn, Step_vn, 0.0f, currentLod->vn);
			} else if (objFile.type == "f") {
				// read face definitions
				// face definitions can contain any number of vertex indices
				// indices are numbered 1 - n, not 0 - n-1, but are converted to 0 - n-1 (where -1 means "no index")
				// for simplicity; store faces > 3 as a fan of triangles
				std::list<std::string> vert;
				ReadParams(objFile, Step_f_idx_elem, vert);
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
						const int size = (int)currentLod->v.size()/Step_v;
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
						const int size = (int)currentLod->vt.size()/Step_vt;
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
						const int size = (int)currentLod->vn.size()/Step_vn;
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
					if (face[face.size()-3] >= ((int)currentLod->v.size()/Step_v)) {
						sout << "Index " << face[face.size()-3]+1 << " is out of defined range for \'v\'";
						AddError(objFile, sout);
					}
					if (face[face.size()-2] >= ((int)currentLod->vt.size()/Step_vt)) {
						sout << "Index " << face[face.size()-2]+1 << " is out of defined range for \'vt\'";
						AddError(objFile, sout);
					}
					if (face[face.size()-1] >= ((int)currentLod->vn.size()/Step_vn)) {
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
							// vertex 3
							currentLod->f.push_back(face[i+Step_f_idx+IndexPos]);
							currentLod->f.push_back(face[i+Step_f_idx+IndexTex]);
							currentLod->f.push_back(face[i+Step_f_idx+IndexNor]);
							// vertex 2
							currentLod->f.push_back(face[i+IndexPos]);
							currentLod->f.push_back(face[i+IndexTex]);
							currentLod->f.push_back(face[i+IndexNor]);
							// vertex 1
							currentLod->f.push_back(face[IndexPos]);
							currentLod->f.push_back(face[IndexTex]);
							currentLod->f.push_back(face[IndexNor]);
							// materials
							currentLod->usemtl.push_back(currentLod->state.usemtl);
							// groups
							currentLod->g.push_back(currentLod->state.g);
						}
					}
				}
			} /*else if (objFile.type == "p") {
				ReadParams(objFile, 1, currentLod->p); // a single "p" can specify any number of points
			} else if (objFile.type == "l") {
				std::list<std::string> lines;
				ReadParams(objFile, 2, lines);
				// divide all lines into segments of 2 vertices
				if (lines.size() > 2) {
					for (size_t i = 0; i < lines.size()-1; ++i) {
					}
				}
				// add vertex texture coordinate index parsing (v or v/vt)
				// check consistency
			}*/
			else if (objFile.type == "g") {
				currentLod->state.g = objFile.params;
			} else if (objFile.type == "usemtl") {
				std::list<std::string> mtlname;
				ReadParams(objFile, 1, 1, std::string(), mtlname);
				std::list<MTL>::const_iterator newmtlIt = currentLod->newmtl.begin();
				for (int i = 0; newmtlIt != currentLod->newmtl.end(); ++i, ++newmtlIt) {
					if (newmtlIt->newmtl == mtlname.front()) {
						currentLod->state.usemtl = i;
						break;
					}
				}
				if (newmtlIt == currentLod->newmtl.end()) {
					sout << "Material \"" << mtlname.front() << "\" not defined";
					AddError(objFile, sout);
					currentLod->state.usemtl = -1;
				}
			}  else if (objFile.type == "mtllib") {
				std::list<std::string> mtlfiles;
				ReadParams(objFile, 1, mtlfiles);
				std::list<std::string>::const_iterator mtlfileIt;
				File mtlFile;
				mtlFile.lineNo = 0;
				for (mtlfileIt = mtlfiles.begin(); mtlfileIt != mtlfiles.end() && !mtlFile.fin.is_open(); ++mtlfileIt) {
					mtlFile.fin.open(std::string(workingDirectory + *mtlfileIt).c_str());
					if (!mtlFile.fin.is_open()) {
						sout << "Could not open \"" << *mtlfileIt << "\"";
						AddWarning(objFile, sout);
					} else {
						mtlFile.name = *mtlfileIt;
					}
				}
				if (mtlFile.fin.is_open()) {

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
					
					std::list<MTL>::iterator mtl = currentLod->newmtl.end();
					while (!mtlFile.fin.eof()) {
						ReadLine(mtlFile);

						if (mtlFile.type == "newmtl") {
							MTL newmtl; // automatically sets up defaults
							std::list<std::string> mtlname;
							ReadParams(mtlFile, 0, 1, std::string("default"), mtlname);
							if (mtlname.size() > 0) { // name is OK
								newmtl.newmtl = mtlname.front();
								for (mtl = currentLod->newmtl.begin(); mtl != currentLod->newmtl.end(); ++mtl) {
									if (mtl->newmtl == newmtl.newmtl) {
										break;
									}
								}
								if (mtl == currentLod->newmtl.end()) { // if you get here, then material name passed all error checks
									currentLod->newmtl.push_back(newmtl);
									mtl = --currentLod->newmtl.end();
								} else {
									sout << "Redefinition of material \"" << mtl->newmtl << "\"";
									AddError(mtlFile, sout);
									mtl = currentLod->newmtl.end(); // set mtl to invalid value
								}
							} else {
								mtl = currentLod->newmtl.end(); // if material name failed mtl is set to invalid value
							}
						} else if (mtl != currentLod->newmtl.end()) {
							//
							// Note
							//
							// Ka, Kd, Ks et al. are not implemented correctly.
							// Read their values as strings, not as floats, since
							// parameters can contain keywords such as "spectral".
							//
							if (mtlFile.type == "Ka") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Ka, Step_Ka, 0.2f, temp); // default values should not matter, since 3 elements are required to be specified
								if (temp.size() > 0) {
									std::list<float>::const_iterator it = temp.begin();
									for (int i = 0; i < Step_Ka; ++i, ++it) {
										mtl->Ka[i] = *it;
									}
								}
							} else if (mtlFile.type == "Kd") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Kd, Step_Kd, 0.8f, temp); // default values should not matter, since 3/3 elements are required to be specified
								if (temp.size() > 0) {
									std::list<float>::const_iterator it = temp.begin();
									for (int i = 0; i < Step_Kd; ++i, ++it) {
										mtl->Kd[i] = *it;
									}
								}
							} else if (mtlFile.type == "Ks") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Ks, Step_Ks, 1.0f, temp); // default values should not matter, since 3/3 elements are required to be specified
								if (temp.size() > 0) {
									std::list<float>::const_iterator it = temp.begin();
									for (int i = 0; i < Step_Ks; ++i, ++it) {
										mtl->Ks[i] = *it;
									}
								}
							} else if (mtlFile.type == "Ke") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Ke, Step_Ke, 0.0f, temp); // default values should not matter, since 3/3 elements are required to be specified
								if (temp.size() > 0) {
									std::list<float>::const_iterator it = temp.begin();
									for (int i = 0; i < Step_Ke; ++i, ++it) {
										mtl->Ke[i] = *it;
									}
								}
							} else if (mtlFile.type == "Tr") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Tr, Step_Tr, 1.0f, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->Tr = temp.front();
								}
							} else if (mtlFile.type == "d") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_d, Step_d, 1.0f, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->d = temp.front();
								}
							} else if (mtlFile.type == "Tf") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Tf, Step_Tf, 1.0f, temp); // default values should not matter, since 3/3 elements are required to be specified
								if (temp.size() > 0) {
									std::list<float>::const_iterator it = temp.begin();
									for (int i = 0; it != temp.end(); ++i, ++it) {
										mtl->Tf[i] = *it;
									}
								}
							} else if (mtlFile.type == "Ns") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Ns, Step_Ns, 0.0f, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->Ns = temp.front();
								}
							} else if (mtlFile.type == "Ni") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_Ni, Step_Ni, 10.0f, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->Ni = temp.front();
								}
							} else if (mtlFile.type == "sharpness") {
								std::list<float> temp;
								ReadParams(mtlFile, Step_sharpness, Step_sharpness, 60.0f, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->sharpness = temp.front();
								}
							} else if (mtlFile.type == "illum") {
								std::list<int> temp;
								ReadParams(mtlFile, Step_illum, Step_illum, 1, temp); // default values should not matter, since 1/1 elements are required to be specified
								if (temp.size() > 0) {
									mtl->illum = temp.front();
								}
							} else if (mtlFile.type == "map_Ka") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Ka = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Ka = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Kd") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Kd = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Kd = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Ks") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Ks = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Ks = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Ke") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Ke = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Ke = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Tf") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Tf = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Tf = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Ns") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Ks = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Ks = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_Tr") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_Tr = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_Tr = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "map_d") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->map_d = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->map_d = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "disp") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->disp = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->disp = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "decal") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->decal = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->decal = *it;
											break;
										}
									}
								}
							} else if (mtlFile.type == "bump") {
								std::list<std::string> tempMap;
								ReadParams(mtlFile, 1, tempMap);
								if (tempMap.size() > 0) {
									File mapFile;
									mtl->bump = "";
									for (std::list<std::string>::const_iterator it = tempMap.begin(); it != tempMap.end() && !mapFile.fin.is_open(); ++it) {
										mapFile.fin.open(it->c_str());
										if (mapFile.fin.is_open()) {
											mtl->bump = *it;
											break;
										}
									}
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
				currentLod->shadow_obj = objFile.params;
			} else if (objFile.type == "lod") {
				std::list<int> lodVal;
				ReadParams(objFile, 1, 1, 0, lodVal);
				if (lodVal.size() == 1) {
					if (currentLod->v.size() == 0 && currentLod->f.size() == 0) { // lod does not contain any relevant data
						sout << "Previous LOD " << currentLod->state.lod << " does not contain any relevant data. Skipping...";
						AddWarning(objFile, sout);
						lodData.erase(currentLod);
					}
					for (currentLod = lodData.begin(); currentLod != lodData.end(); ++currentLod) {
						if (lodVal.front() >= currentLod->state.lod) {
							break;
						}
					}
					ObjData newLod;
					currentLod = lodData.insert(currentLod, newLod);
				}
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
		if (currentLod->f.size() == 0) {
			sout << "File does not contain any face definitions";
			warnings.push_back(sout.str());
			sout.str("");
		}
	} else {
		sout << "\"" << objFile.name << "\": File could not be opened";
		errors.push_back(sout.str());
		sout.str("");
	}

	// create the main data structure
	if (errors.size() == 0) {
		
		// sort lod:s by number at time of adding
		// place lod:s so that you access next level of detail by accessing lod->v ... lod->lod->lod->v and so on...
		OBJ *lodPtr = this;
		currentLod = lodData.begin();

		do {
			CreateArrayFromList(currentLod->v, &lodPtr->v, lodPtr->num_v);
			CreateArrayFromList(currentLod->vt, &lodPtr->vt, lodPtr->num_vt);
			CreateArrayFromList(currentLod->vn, &lodPtr->vn, lodPtr->num_vn);
			CreateArrayFromList(currentLod->newmtl, &lodPtr->newmtl, lodPtr->num_newmtl);
			CreateArrayFromList(currentLod->g, &lodPtr->g, lodPtr->num_g);
			CreateArrayFromList(currentLod->usemtl, &lodPtr->usemtl, lodPtr->num_usemtl);
			CreateArrayFromList(currentLod->f, &lodPtr->f, lodPtr->num_f);
			//CreateArrayFromList(currentLod->p, &lodPtr->p, lodPtr->num_p);
			//CreateArrayFromList(currentLod->l, &lodPtr->l, lodPtr->num_l);
			lodPtr->file = file;
			lodPtr->o = o;
			lodPtr->shadow_obj = currentLod->shadow_obj;

			// models are made for looking down the negative z axis
			// engine looks down the positive z axis
			// 1. reverse triangle winding order (this is done when triangles are read)
			// 2. negate model's z coordinates
			// 3. negate model's normals' z coordinates
			for (int i = 0; i < lodPtr->num_v; i+=Step_v) { // Invert z axis
				lodPtr->v[i+2] = -lodPtr->v[i+2];
			}
			for (int i = 0; i < lodPtr->num_vn; i+=Step_vn) { // Invert normals' z axis
				lodPtr->vn[i+2] = -lodPtr->vn[i+2];
			}

			if (++currentLod != lodData.end()) {
				lodPtr->lod = new OBJ;
				lodPtr = lodPtr->lod;
			} else {
				lodPtr = NULL; // should be NULL automatically, but just to make sure...
				break;
			}
		} while (true);
	}
}

void OBJ::Free( void )
{
	Free(this);
	errors.clear();
	warnings.clear();
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

// models are made for looking down the negative z axis
// engine looks down the positive z axis
// 1. reverse triangle winding order
// 2. negate model's z coordinates (will this muck with winding order, i.e. do I need to change BOTH winding order and z coordinates - if no, change z coordinates)
// 3. negate model's normals' z coordinates
void OBJ::Reverse( void )
{
	struct Vertex {
		int v, vt, vn;
	};
	struct Face {
		Vertex v1, v2, v3;
	};

	OBJ *lod = this;
	do {
		// models are made for looking down the negative z axis
		// engine looks down the positive z axis
		// 1. reverse triangle winding order
		// 2. negate model's z coordinates (will this muck with winding order, i.e. do I need to change BOTH winding order and z coordinates - if no, change z coordinates)
		// 3. negate model's normals' z coordinates
		const int NUM_FACES = num_f / OBJ::Step_f_idx;
		Face * const face = (Face * const)f;
		for (int i = 0; i < NUM_FACES; ++i) {
			Vertex temp = face[i].v1;
			face[i].v1 = face[i].v3;
			face[i].v3 = temp;
		}
		
		for (int i = 0; i < lod->num_v; i+=Step_v) { // Invert z axis
			lod->v[i+2] = -lod->v[i+2];
		}
		for (int i = 0; i < lod->num_vn; i+=Step_vn) { // Invert normals
			lod->vn[i+2] = -lod->vn[i+2];
		}
		lod = lod->lod;
	} while (lod != NULL);
}

#ifdef _DEBUG // MSVC define for debug compilation
void OBJ::DumpContents(std::ostream &out) const
{
	int lodNum = 1;
	for (const OBJ *l = this; l != NULL; l = l->lod, ++lodNum) {
		out << "lod = " << lodNum << std::endl;
		out << "o = " << l->o << std::endl;
		out << "shadow_obj " << l->shadow_obj << std::endl;
		out << "num v = " << l->num_v << std::endl;
		for (int i = 0; i < l->num_v; i+=Step_v) {
			out << "v " << l->v[i] << " " << l->v[i+1] << " " << l->v[i+2] << " " << l->v[i+3] << std::endl;
		}
		out << "num vt = " << l->num_vt << std::endl;
		for (int i = 0; i < l->num_vt; i+=Step_vt) {
			out << "vt " << l->vt[i] << " " << l->vt[i+1] << " " << l->vt[i+2] << std::endl;
		}
		out << "num vn = " << l->num_vn << std::endl;
		for (int i = 0; i < l->num_vn; i+=Step_vn) {
			out << "vn " << l->vn[i] << " " << l->vn[i+1] << " " << l->vn[i+2] << std::endl;
		}
		out << "num f = " << l->num_f << std::endl;
		for (int i = 0; i < l->num_f; i+=Step_f) {
			out << "g " << l->g[i/Step_f] << std::endl;
			out << "usemtl " << l->usemtl[i/Step_f] << std::endl;
			out << "f ";
			for (int j = i; j < i+Step_f; j+=Step_f_idx) {
				for (int n = j; n < j+Step_f_idx_elem-1; ++n) {
					out << l->f[n]+1 << "/";
				}
				out << l->f[j+Step_f_idx_elem-1]+1 << " ";
			}
			out << std::endl;
		}
		out << "num newmtl = " << l->num_usemtl << std::endl;
		for (int i = 0; i < l->num_newmtl; ++i) {
			out << "newmtl " << l->newmtl[i].newmtl << std::endl;
			out << "Ka     " << l->newmtl[i].Ka[0] << " " << l->newmtl[i].Ka[1] << " " << l->newmtl[i].Ka[2] << std::endl;
			out << "Kd     " << l->newmtl[i].Kd[0] << " " << l->newmtl[i].Kd[1] << " " << l->newmtl[i].Kd[2] << std::endl;
			out << "Ks     " << l->newmtl[i].Ks[0] << " " << l->newmtl[i].Ks[1] << " " << l->newmtl[i].Ks[2] << std::endl;
			out << "Ke     " << l->newmtl[i].Ke[0] << " " << l->newmtl[i].Ke[1] << " " << l->newmtl[i].Ke[2] << std::endl;
			out << "Tf     " << l->newmtl[i].Tf[0] << " " << l->newmtl[i].Tf[1] << " " << l->newmtl[i].Tf[2] << std::endl;
			out << "Tr     " << l->newmtl[i].Tr << std::endl;
			out << "d      " << l->newmtl[i].d << std::endl;
			out << "Ns     " << l->newmtl[i].Ns << std::endl;
			out << "Ni     " << l->newmtl[i].Ni << std::endl;
			out << "illum  " << l->newmtl[i].illum << std::endl;
			out << "map_Ka " << l->newmtl[i].map_Ka << std::endl;
			out << "map_Kd " << l->newmtl[i].map_Kd << std::endl;
			out << "map_Ks " << l->newmtl[i].map_Ks << std::endl;
			out << "map_Ke " << l->newmtl[i].map_Ke << std::endl;
			out << "map_Tf " << l->newmtl[i].map_Tf << std::endl;
			out << "map_Ns " << l->newmtl[i].map_Ns << std::endl;
			out << "map_Tr " << l->newmtl[i].map_Tr << std::endl;
			out << "map_d  " << l->newmtl[i].map_d << std::endl;
			out << "disp   " << l->newmtl[i].disp << std::endl;
			out << "decal  " << l->newmtl[i].decal << std::endl;
			out << "bump   " << l->newmtl[i].bump << std::endl;
		}
	}
}
#endif
