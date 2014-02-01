// Copyright (c) Jonathan Karlsson 2011-2012
// Code may be used freely for commercial and non-commercial purposes.
// Author retains his moral rights under the applicable copyright laws
// (i.e. credit the author where credit is due).
//

#ifndef WAVEFRONTOBJ_H_INCLUDED__
#define WAVEFRONTOBJ_H_INCLUDED__

#include <list>
#include <string>
#include <sstream>
#include <fstream>

class OBJ
{
private:
	static const int Step_f_idx_elem = 3; // number of elements per vertex index cluster
	static const int Step_f_idx = 3; // number of vertex index clusters (v/vt/vn) per face
	static const int Step_f = Step_f_idx*Step_f_idx_elem; // polygons are always split into triangles
	static const int IndexPos = 0;
	static const int IndexTex = 1;
	static const int IndexNor = 2;
public:
	
	template < typename type_t, int size_i >
	class Array
	{
	private:
		type_t arr[size_i];
	public:
		operator type_t*( void ) { return arr; }
		operator const type_t*( void ) const { return arr; }
	};
	
	typedef Array<float,4> float4;
	typedef Array<float,3> float3;
	typedef Array<int,3> index3;
	
	typedef std::list<float4> VertexList;
	typedef std::list<float3> TexCoordList;
	typedef std::list<float3> NormalList;
	
	enum { X, Y, Z, W };
	enum { U, V, Q };
	
	struct Facet
	{
		static const int MISSING_INDEX = -1;
		static const int DEFAULT_MATERIAL = 0;
		
		index3 vertex;
		index3 texCoord;
		index3 normal;
		int material;
	};
	typedef std::list<Facet> FacetList;
	typedef std::list<int> FacetIndexList;
	
	struct Group
	{
		// would be awesome to group faces by materials within a Group
		// reduces state changes in OpenGL, but maybe I should leave that
		// to the app using the importer.
		std::string name;
		FacetIndexList facets;
		Group( void ) : name("default"), facets() {}
	};
	typedef std::list<Group> GroupList;
	
	class Material
	{
	public:
		static const int FLAT = 0;
		static const int DIFFUSE = 1;
		static const int DIFFUSE_AND_SPECULAR = 2;
	public:
		std::string name;
		float4 ambient; // Ka
		float4 diffuse; // Kd
		float4 specular; // Ks;
		float4 emissive; // Ke
		float4 transmission; // Tf
		float alpha; // Tr
		float dissolve; // d
		float shininess; // Ns
		float opticalDensity; // Ni
		float sharpness; // sharpness
		int illumination; // illum
		std::string ambientMap;
		std::string diffuseMap;
		std::string specularMap;
		std::string emissiveMap;
		std::string transmissionMap;
		std::string shininessMap;
		std::string alphaMap;
		std::string dissolveMap;
		std::string displacementMap;
		std::string detailMap;
		std::string bumpMap;
	public:
		Material( void );
	};
	typedef std::list<Material> MaterialList;
	
	class LevelOfDetail
	{
	public:
		// vertex properties
		VertexList vertices;
		TexCoordList texCoords;
		NormalList normals;
		// face definition and properties
		FacetList facets;
		GroupList groups;
		// level of detail info
		int levelOfDetail;
	};
	typedef std::list<LevelOfDetail> LODList;
private:
	struct File
	{
		std::ifstream fin;
		std::string name;
		int lineNo;
		std::string type;
		std::string params;
	};

	struct StateVariables
	{
		LODList::iterator LOD;
		std::list<GroupList::iterator> groups;
		MaterialList::iterator material;
		int materialIndex;
		StateVariables( void ) : materialIndex(0) {}
	};
private:
	OBJ( void );
private:
	bool Open(File &file, const std::string &filename);
	void ReadLine(File &file) const;
	void AddError(const File &file, std::ostringstream &sout);
	void AddWarning(const File &file, std::ostringstream &sout);
	template < typename type_t >
	void Swap(type_t &val1, type_t &val2) const;
	template < typename type_t >
	void ReadParams(const File &file, int minParams, int maxParams, const type_t &defaultValue, type_t *out);
	template < typename type_t >
	void ReadParams(const File &file, int params, type_t *out);
	template < typename T >
	void ReadVariableParams(const File &file, int minParams, std::list<T> &out);
public:
	std::string fileName;
	std::string name;
	std::string shadowModel;
	LODList levelOfDetail;
	MaterialList materials;
private:
	std::list<std::string> errors;
	std::list<std::string> warnings;
public:
	explicit OBJ(const std::string &filename);
public:
	enum Status
	{
		OK,
		WARNINGS,
		ERRORS
	};
public:
	Status GetStatus( void ) const;
	void Reverse( void );
	bool HasErrors( void ) const { return !errors.empty(); }
	bool HasWarnings( void ) const { return !warnings.empty(); }
	void DumpErrors(std::ostream &out, const unsigned int MaxErrors) const;
	void DumpWarnings(std::ostream &out, const unsigned int MaxErrors) const;
};

template < typename type_t >
void OBJ::Swap(type_t &val1, type_t &val2) const
{
	type_t temp = val1;
	val1 = val2;
	val2 = temp;
}

template < typename type_t >
void OBJ::ReadParams(const OBJ::File &file, int minParams, int maxParams, const type_t &defaultValue, type_t *out)
{
	std::istringstream sin(file.params);
	int numParams = 0;
	
	while (numParams < maxParams && sin >> out[numParams]) {
		++numParams;
	}
		
	// if too many arguments, count them
	type_t value;
	while (sin >> value) {
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
	} else {
		for (int i = numParams; i < maxParams; ++i) {
			out[i] = defaultValue;
		}
	}
}

template < typename type_t >
void OBJ::ReadParams(const OBJ::File &file, int params, type_t *out)
{
	type_t temp;
	ReadParams(file, params, params, temp, out);
}

template < typename T >
void OBJ::ReadVariableParams(const OBJ::File &file, int minParams, std::list<T> &out)
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
		sout << "\'" << file.type << "\' does not take " << numParams << " parameter(s) (expected at least " << minParams << ")";
		AddError(file, sout);
		for (int i = 0; i < numParams; ++i) {
			out.pop_back();
		}
	}
}

#endif
