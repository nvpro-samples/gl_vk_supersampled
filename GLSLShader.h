#pragma once
#ifdef WIN32
#  include <windows.h>
#endif
#include "GL/glew.h"
#include <vector>
#include <string>

class GLSLShader 
{
public:
	GLSLShader();
	~GLSLShader();

	void cleanup();

	bool addFragmentShader(const char* filename, bool isNew=true);
	bool addVertexShader(const char* filename, bool isNew=true);
	bool addFragmentShaderFromString(const char* shader);
	bool addVertexShaderFromString(const char* shader); 
    bool link();

	bool bindShader();
	void unbindShader();

	void setUniformFloat(const char* name, float val);
	void setUniformInt(const char* name, int val);
	void setUniformVector(const char * name, float* val, int count);
	void setTextureUnit(const char * texname, int texunit);
	void bindTexture(GLenum target, const char * texname, GLuint texid, int texunit);
	
	void reloadShader();

	inline int getProgram() {return m_program;}

	inline int getUniformLocation(const char* name) { return glGetUniformLocation(m_program, name); }

private:

	bool compileShader(const char* filename, GLenum type);
	bool compileShaderFromString(const char *shader, GLenum type);
	bool outputLog(GLhandleARB obj);

	bool m_bound;
	bool m_linkNeeded;

	std::vector<std::string> m_vertFiles;
	std::vector<std::string> m_fragFiles;
	std::vector<std::string> m_vertSrc;
	std::vector<std::string> m_fragSrc;

	GLhandleARB m_program;

};