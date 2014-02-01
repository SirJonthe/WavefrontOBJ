// Copyright (c) Jonathan Karlsson 2011
// Code may be used freely for commercial and non-commercial purposes.
// Author retains his moral rights under the applicable copyright laws
// (i.e. credit the author where credit is due).
//

#ifndef OBJPARSER_H_INCLUDED__
#define OBJPARSER_H_INCLUDED__

#include <list>
#include <string>
#include <sstream>
#include <fstream>

class MTL
{
public:
	std::string newmtl;
	float Ka[3]; // ambient color
	float Kd[3]; // diffuse color
	float Ks[3]; // specular color
	float Ke[3]; // emissive color
	float Tf[3]; // transmission filter (specifies what colors are allowed to pass through translucent objects, 1;1;1 is then default)
	float Tr; // alpha
	float d; // dissolve (NOTE: according to some sources Tr and d are the same thing)
	float Ns; // shininess exponent
	float Ni; // optical density (DEFAULT VALUE??)
	float sharpness; // sharpness of reflections
	int illum; // illumination model (0=flat, 1=diffuse, 2=diffuse+specular) http://cgkit.sourceforge.net/doc2/objmaterial.html
	std::string map_Ka; // ambient texture map
	std::string map_Kd; // diffuse texture map
	std::string map_Ks; // specular texture map
	std::string map_Ke; // emissive texture map
	std::string map_Tf; // transmission filter texture map
	std::string map_Ns; // shininess texture map
	std::string map_Tr; // alpha texture map
	std::string map_d; // dissolve texture map (NOTE: according to some sources Tr and d are the same thing)
	std::string disp;
	std::string decal; // NOTE: Not sure how this works, but I'm guessing its supposed to specify a detail map
	std::string bump;
public:
	MTL( void );
};

class OBJ
{
public:
	static const int Step_v = 4;
	static const int Step_vt = 3;
	static const int Step_vn = 3;
	static const int Step_f_idx_elem = 3; // number of elements per vertex index cluster
	static const int Step_f_idx = 3; // number of vertex index clusters (v/vt/vn) per face
	static const int Step_usemtl = 3;
	static const int Step_f = Step_f_idx*Step_f_idx_elem; // polygons are always split into triangles
	static const int Step_Ka = 3;
	static const int Step_Kd = 3;
	static const int Step_Ks = 3;
	static const int Step_Ke = 3;
	static const int Step_Tr = 1;
	static const int Step_d = 1;
	static const int Step_Tf = 3;
	static const int Step_Ns = 1;
	static const int Step_Ni = 1;
	static const int Step_illum = 1;
	static const int Step_sharpness = 1;
private:
	static const int IndexPos = 0;
	static const int IndexTex = 1;
	static const int IndexNor = 2;
private:
	struct File
	{
		std::ifstream fin;
		std::string name;
		int lineNo;
		std::string type;
		std::string params;
	};
	struct ObjData
	{
		
		std::list<float> v, vn, vt;
		std::list<MTL> newmtl;
		std::list<int> f, p, l, usemtl;
		std::list<std::string> g;
		std::string shadow_obj;
		struct
		{
			int lod;
			int usemtl;
			std::string g;
			void Reset( void )
			{
				this->lod = 0;
				this->usemtl = -1;
				this->g = "default";
			}
		} state;
		ObjData( void )
		{
			state.Reset();
		}
	};
private:
	OBJ( void );
private:
	void ReadLine(File &file) const;
	void AddError(const File &file, std::ostringstream &sout);
	void AddWarning(const File &file, std::ostringstream &sout);
	void Free(OBJ *LOD);
	template < typename T >
	void ReadParams(const File &file, int minParams, int maxParams, const T &defaultValue, std::list<T> &out);
	template < typename T >
	void ReadParams(const File &file, int minParams, std::list<T> &out);
	template < typename T >
	void CreateArrayFromList(const std::list<T> &list, T **array, int &arraySize);
public:
	std::string file;
	std::string o;
	std::string shadow_obj; // the filename (.obj) of the model that the loaded model will be using as its shadow (usually itself, or a file containing a lower res model)
	// vertex properties
	float *v; // positions
	float *vt; // texture coordinates
	float *vn; // surface/vertex normals
	// material properties
	MTL *newmtl; // stores associated materials
	// face definition and properties
	int *f; // vertex index of triangles - converted and stored as triangles
	int *usemtl; // what material the face uses - a material is always stored per face, even if not explicitly in the .obj file
	std::string *g; // a group of tokens that identify faces - a group is always stored per face, even if not explicitly in the .obj file
	// next level of detail
	OBJ *lod; // pointer to model containing the next level of detail (freeing this memory requires recursive freeing i.e. delete this->lod...->lod)
	// size properies
	int num_v;
	int num_vt;
	int num_vn;
	int num_f;
	int num_usemtl;
	int num_g;
	int num_newmtl;
private:
	std::list<std::string> errors;
	std::list<std::string> warnings;
public:
	// ASSUMES MODEL IS REVERSED (i.e. camera looking down -z)
	// Fixes this by:
	// Reversing winding order
	// Negating z coordinates
	// Inverting normals
	explicit OBJ(const std::string &filename);
public:
	bool HasErrors( void ) const { return errors.size() != 0; }
	bool HasWarnings( void ) const { return warnings.size() != 0; }
	void Free( void );
	void DumpErrors(std::ostream &out, const unsigned int MaxErrors=50) const;
	void DumpWarnings(std::ostream &out, const unsigned int MaxWarnings=50) const;
	void Reverse( void );
#ifdef _DEBUG // MSVC define for debug compilation
	void DumpContents(std::ostream &out) const;
#endif
};

template < typename T >
void OBJ::CreateArrayFromList(const std::list<T> &list, T **array, int &arraySize)
{
	arraySize = (int)list.size();
	*array = new T[arraySize];
	int i = 0;
	for (typename std::list<T>::const_iterator it = list.begin(); it != list.end(); ++it, ++i) {
		(*array)[i] = *it;
	}
}

template < typename T >
void OBJ::ReadParams(const File &file, int minParams, int maxParams, const T &defaultValue, std::list<T> &out)
{
	std::istringstream sin(file.params);
	int numParams = 0;
	T value;
	while (sin >> value) {
		out.push_back(value);
		++numParams;
	}
	if (numParams < minParams || numParams > maxParams) {
		std::ostringstream sout;
		sout << "\'" << file.type << "\' does not take " << numParams << " parameter(s) (expected " << minParams;
		if (minParams != maxParams) {
			 sout << "-" << maxParams;
		}
		sout << ")";
		AddError(file, sout);
		for (int i = 0; i < numParams; ++i) {
			out.erase(--out.end()); // not sure this will work
		}
	} else {
		for (int i = numParams; i < maxParams; ++i) {
			out.push_back(defaultValue);
		}
	}
}

template < typename T >
void OBJ::ReadParams(const File &file, int minParams, std::list<T> &out)
{
	std::istringstream sin(file.params);
	int numParams = 0;
	T value;
	while (sin >> value) {
		out.push_back(value);
		++numParams;
	}
	if (numParams < minParams) {
		std::ostringstream sout;
		sout << "\'" << file.type << "\' does not take " << numParams << " parameter(s) (expected " << minParams << ")";
		AddError(file, sout);
		for (int i = 0; i < numParams; ++i) {
			//out.erase(--out.end()); // not sure this will work
			out.pop_back();
		}
	}
}

#endif
