/* Copyright 2013 Jeremie Roy. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
*/
#include "text_buffer_manager.h"
#include "../cube_atlas.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>     /* offsetof */

const uint16_t MAX_TEXT_BUFFER_COUNT = 64;

long int fsize(FILE* _file)
{
	long int pos = ftell(_file);
	fseek(_file, 0L, SEEK_END);
	long int size = ftell(_file);
	fseek(_file, pos, SEEK_SET);
	return size;
}

static const bgfx::Memory* loadShader(const char* _shaderPath, const char* _shaderName)
{
	char out[512];
	strcpy(out, _shaderPath);
	strcat(out, _shaderName);
	strcat(out, ".bin");

	FILE* file = fopen(out, "rb");
	if (NULL != file)
	{
		uint32_t size = (uint32_t)fsize(file);
		const bgfx::Memory* mem = bgfx::alloc(size+1);
		/*size_t ignore =*/ fread(mem->data, 1, size, file);
		/*BX_UNUSED(ignore);*/
		fclose(file);
		mem->data[mem->size-1] = '\0';
		return mem;
	}

	return NULL;
}

// Table from Flexible and Economical UTF-8 Decoder
// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

inline uint32_t utf8_decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state*16 + type];
  return *state;
}

inline int utf8_strlen(uint8_t* s, size_t* count) {
  uint32_t codepoint;
  uint32_t state = 0;

  for (*count = 0; *s; ++s)
    if (!utf8_decode(&state, &codepoint, *s))
      *count += 1;

  return state != UTF8_ACCEPT;
}


class TextBuffer
{
public:	
	
	/// TextBuffer is bound to a fontManager for glyph retrieval
	/// @remark the ownership of the manager is not taken
	TextBuffer(FontManager* _fontManager);
	~TextBuffer();

	void setStyle(uint32_t _flags = STYLE_NORMAL) { m_styleFlags = _flags; }
	void setTextColor(uint32_t _rgba = 0x000000FF) { m_textColor = toABGR(_rgba); }
	void setBackgroundColor(uint32_t _rgba = 0x000000FF) { m_backgroundColor = toABGR(_rgba); }

	void setOverlineColor(uint32_t _rgba = 0x000000FF) { m_overlineColor = toABGR(_rgba); }
	void setUnderlineColor(uint32_t _rgba = 0x000000FF) { m_underlineColor = toABGR(_rgba); }
	void setStrikeThroughColor(uint32_t _rgba = 0x000000FF) { m_strikeThroughColor = toABGR(_rgba); }
	
	void setPenPosition(float _x, float _y) { m_penX = _x; m_penY = _y; }
	
	/// return the size of the text 
	//Rectangle measureText(FontHandle _fontHandle, const char * _string);
	//Rectangle measureText(FontHandle _fontHandle, const wchar_t * _string);

	/// append an ASCII/utf-8 string to the buffer using current pen position and color
	void appendText(FontHandle _fontHandle, const char * _string);

	/// append a wide char unicode string to the buffer using current pen position and color
	void appendText(FontHandle _fontHandle, const wchar_t * _string);	

	/// Clear the text buffer and reset its state (pen/color)
	void clearTextBuffer();
	
	/// get pointer to the vertex buffer to submit it to the graphic card
	const uint8_t* getVertexBuffer(){ return (uint8_t*) m_vertexBuffer; }
	/// number of vertex in the vertex buffer
	uint32_t getVertexCount(){ return m_vertexCount; }
	/// size in bytes of a vertex
	uint32_t getVertexSize(){ return sizeof(TextVertex); }
		
	/// get a pointer to the index buffer to submit it to the graphic
	const uint16_t* getIndexBuffer(){ return m_indexBuffer; }
	/// number of index in the index buffer
	uint32_t getIndexCount(){ return m_indexCount; }
	/// size in bytes of an index
	uint32_t getIndexSize(){ return sizeof(uint16_t); }

	uint32_t getTextColor(){ return toABGR(m_textColor); }
private:
	void appendGlyph(CodePoint_t _codePoint, const FontInfo& _font, const GlyphInfo& _glyphInfo);
	void verticalCenterLastLine(float _txtDecalY, float _top, float _bottom);
	uint32_t toABGR(uint32_t _rgba) 
{ 
	return (((_rgba >> 0) & 0xff) << 24) |  
		(((_rgba >> 8) & 0xff) << 16) |    
		(((_rgba >> 16) & 0xff) << 8) |    
		(((_rgba >> 24) & 0xff) << 0);   
}

	static const uint32_t MAX_BUFFERED_CHARACTERS = 8192;

	uint32_t m_styleFlags;

	// color states
	uint32_t m_textColor;

	uint32_t m_backgroundColor;
	uint32_t m_overlineColor;
	uint32_t m_underlineColor;
	uint32_t m_strikeThroughColor;

	//position states	
	float m_penX;
	float m_penY;

	float m_originX;
	float m_originY;	

	float m_lineAscender;
	float m_lineDescender;
	float m_lineGap;
	
	///
	FontManager* m_fontManager;	
	
	void setVertex(uint32_t _i, float _x, float _y, uint32_t _rgba, uint8_t _style = STYLE_NORMAL)
	{
		m_vertexBuffer[_i].x = _x;
		m_vertexBuffer[_i].y = _y;		
		m_vertexBuffer[_i].rgba = _rgba;
		m_styleBuffer[_i] = _style;
	}

	struct TextVertex
	{		
		float x,y;
		int16_t u,v,w,t;
		uint32_t rgba;		
	};

	TextVertex* m_vertexBuffer;
	uint16_t* m_indexBuffer;
	uint8_t* m_styleBuffer;
	
	uint32_t m_vertexCount;
	uint32_t m_indexCount;
	uint32_t m_lineStartIndex;	
};




TextBuffer::TextBuffer(FontManager* _fontManager)
{		
	m_styleFlags = STYLE_NORMAL;
	//0xAABBGGRR
	m_textColor = 0xFFFFFFFF;
	m_backgroundColor = 0xFFFFFFFF;
	m_backgroundColor = 0xFFFFFFFF;
	m_overlineColor = 0xFFFFFFFF;
	m_underlineColor = 0xFFFFFFFF;
	m_strikeThroughColor = 0xFFFFFFFF;
	m_penX = 0;
	m_penY = 0;
	m_originX = 0;
	m_originY = 0;
	m_lineAscender = 0;
	m_lineDescender = 0;
	m_lineGap = 0;
	m_fontManager = _fontManager;	

	
	m_vertexBuffer = new TextVertex[MAX_BUFFERED_CHARACTERS * 4];
	m_indexBuffer = new uint16_t[MAX_BUFFERED_CHARACTERS * 6];
	m_styleBuffer = new uint8_t[MAX_BUFFERED_CHARACTERS * 4];
	m_vertexCount = 0;
	m_indexCount = 0;
	m_lineStartIndex = 0;
	
	
}

TextBuffer::~TextBuffer()
{
	delete[] m_vertexBuffer;
	delete[] m_indexBuffer;
}

void TextBuffer::appendText(FontHandle _fontHandle, const char * _string)
{	
	GlyphInfo glyph;
	const FontInfo& font = m_fontManager->getFontInfo(_fontHandle);	
		
	if(m_vertexCount == 0)
	{
		m_originX = m_penX;
		m_originY = m_penY;
		m_lineDescender = 0;// font.m_descender;
		m_lineAscender = 0;//font.m_ascender;
	}
	
	uint32_t codepoint;
	uint32_t state = 0;

	for (; *_string; ++_string)
		if (!utf8_decode(&state, &codepoint, *_string))
		{
			if(m_fontManager->getGlyphInfo(_fontHandle, (CodePoint_t)codepoint, glyph))
			{
				appendGlyph((CodePoint_t)codepoint, font, glyph);
			}else
			{
				assert(false && "Glyph not found");
			}
		}
	  //printf("U+%04X\n", codepoint);

	if (state != UTF8_ACCEPT)
	{
	//	assert(false && "The string is not well-formed");
		return; //"The string is not well-formed\n"
	}
}

void TextBuffer::appendText(FontHandle _fontHandle, const wchar_t * _string)
{		
	GlyphInfo glyph;
	const FontInfo& font = m_fontManager->getFontInfo(_fontHandle);	
	
	if(m_vertexCount == 0)
	{
		m_originX = m_penX;
		m_originY = m_penY;
		m_lineDescender = 0;// font.m_descender;
		m_lineAscender = 0;//font.m_ascender;
		m_lineGap = 0;
	}

	//parse string
	for( uint32_t ii=0, end = wcslen(_string) ; ii < end; ++ii )
	{
		//if glyph cached, continue
		uint32_t _codePoint = _string[ii];
		if(m_fontManager->getGlyphInfo(_fontHandle, _codePoint, glyph))
		{
			appendGlyph(_codePoint, font, glyph);
		}else
		{
			assert(false && "Glyph not found");
		}
	}
}
/*
TextBuffer::Rectangle TextBuffer::measureText(FontHandle _fontHandle, const char * _string)
{	
}

TextBuffer::Rectangle TextBuffer::measureText(FontHandle _fontHandle, const wchar_t * _string)
{
}
*/

void TextBuffer::clearTextBuffer()
{
	m_vertexCount = 0;
	m_indexCount = 0;
	m_lineStartIndex = 0;
	m_lineAscender = 0;
	m_lineDescender = 0;
}

void TextBuffer::appendGlyph(CodePoint_t _codePoint, const FontInfo& _font, const GlyphInfo& _glyphInfo)
{	
	//handle newlines
	if(_codePoint == L'\n' )
    {
        m_penX = m_originX;
        m_penY -= m_lineDescender;
		m_penY += m_lineGap;
		m_lineDescender = 0;
		m_lineAscender = 0;
        m_lineStartIndex = m_vertexCount;
		return;
    }
	
	if( _font.m_ascender > m_lineAscender || (_font.m_descender < m_lineDescender) )
    {
		if( _font.m_descender < m_lineDescender )
		{
			m_lineDescender = _font.m_descender;
			m_lineGap = _font.m_lineGap;
		}
				
		float txtDecals = (_font.m_ascender - m_lineAscender);
		m_lineAscender = _font.m_ascender;
		m_lineGap = _font.m_lineGap;		
				
		m_penY += txtDecals;
		verticalCenterLastLine((txtDecals), (m_penY - m_lineAscender), (m_penY - m_lineDescender+m_lineGap));		
    }
			
	//handle kerning
	float kerning = 0;
	/*	
    if( previous && markup->font->kerning )
    {
        kerning = texture_glyph_get_kerning( glyph, previous );
    }
	*/
	m_penX += kerning * _font.m_scale;

	GlyphInfo& blackGlyph = m_fontManager->getBlackGlyph();
	
	if( m_styleFlags & STYLE_BACKGROUND && m_backgroundColor & 0xFF000000)
	{
		float x0 = ( m_penX - kerning );
		float y0 = ( m_penY  - m_lineAscender);
		float x1 = ( (float)x0 + (_glyphInfo.m_advance_x));
		float y1 = ( m_penY - m_lineDescender + m_lineGap );

		m_fontManager->getAtlas()->packUV(blackGlyph.m_regionIndex, (uint8_t*)m_vertexBuffer,sizeof(TextVertex) *m_vertexCount + offsetof(TextVertex, u), sizeof(TextVertex));

		setVertex(m_vertexCount+0, x0, y0, m_backgroundColor,STYLE_BACKGROUND);
		setVertex(m_vertexCount+1, x0, y1, m_backgroundColor,STYLE_BACKGROUND);
		setVertex(m_vertexCount+2, x1, y1, m_backgroundColor,STYLE_BACKGROUND);
		setVertex(m_vertexCount+3, x1, y0, m_backgroundColor,STYLE_BACKGROUND);

		m_indexBuffer[m_indexCount + 0] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 1] = m_vertexCount+1;
		m_indexBuffer[m_indexCount + 2] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 3] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 4] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 5] = m_vertexCount+3;
		m_vertexCount += 4;
		m_indexCount += 6;
	}
	
	if( m_styleFlags & STYLE_UNDERLINE && m_underlineColor & 0xFF000000)
	{
		float x0 = ( m_penX - kerning );
		float y0 = (m_penY - m_lineDescender/2 );
		float x1 = ( (float)x0 + (_glyphInfo.m_advance_x));
		float y1 = y0+_font.m_underline_thickness;

		m_fontManager->getAtlas()->packUV(blackGlyph.m_regionIndex, (uint8_t*)m_vertexBuffer,sizeof(TextVertex) *m_vertexCount + offsetof(TextVertex, u), sizeof(TextVertex));

		setVertex(m_vertexCount+0, x0, y0, m_underlineColor,STYLE_UNDERLINE);
		setVertex(m_vertexCount+1, x0, y1, m_underlineColor,STYLE_UNDERLINE);
		setVertex(m_vertexCount+2, x1, y1, m_underlineColor,STYLE_UNDERLINE);
		setVertex(m_vertexCount+3, x1, y0, m_underlineColor,STYLE_UNDERLINE);

		m_indexBuffer[m_indexCount + 0] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 1] = m_vertexCount+1;
		m_indexBuffer[m_indexCount + 2] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 3] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 4] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 5] = m_vertexCount+3;
		m_vertexCount += 4;
		m_indexCount += 6;
	}
	
	if( m_styleFlags & STYLE_OVERLINE && m_overlineColor & 0xFF000000)
	{
		float x0 = ( m_penX - kerning );
		float y0 = (m_penY - _font.m_ascender );
		float x1 = ( (float)x0 + (_glyphInfo.m_advance_x));
		float y1 = y0+_font.m_underline_thickness;

		m_fontManager->getAtlas()->packUV(blackGlyph.m_regionIndex, (uint8_t*)m_vertexBuffer,sizeof(TextVertex) *m_vertexCount + offsetof(TextVertex, u), sizeof(TextVertex));

		setVertex(m_vertexCount+0, x0, y0, m_overlineColor,STYLE_OVERLINE);
		setVertex(m_vertexCount+1, x0, y1, m_overlineColor,STYLE_OVERLINE);
		setVertex(m_vertexCount+2, x1, y1, m_overlineColor,STYLE_OVERLINE);
		setVertex(m_vertexCount+3, x1, y0, m_overlineColor,STYLE_OVERLINE);

		m_indexBuffer[m_indexCount + 0] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 1] = m_vertexCount+1;
		m_indexBuffer[m_indexCount + 2] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 3] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 4] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 5] = m_vertexCount+3;
		m_vertexCount += 4;
		m_indexCount += 6;
	}
	
		
	if( m_styleFlags & STYLE_STRIKE_THROUGH && m_strikeThroughColor & 0xFF000000)
	{
 		float x0 = ( m_penX - kerning );
		float y0 = (m_penY - _font.m_ascender/3 );
		float x1 = ( (float)x0 + (_glyphInfo.m_advance_x) );
		float y1 = y0+_font.m_underline_thickness;
		
		m_fontManager->getAtlas()->packUV(blackGlyph.m_regionIndex, (uint8_t*)m_vertexBuffer,sizeof(TextVertex) *m_vertexCount + offsetof(TextVertex, u), sizeof(TextVertex));

		setVertex(m_vertexCount+0, x0, y0, m_strikeThroughColor,STYLE_STRIKE_THROUGH);
		setVertex(m_vertexCount+1, x0, y1, m_strikeThroughColor,STYLE_STRIKE_THROUGH);
		setVertex(m_vertexCount+2, x1, y1, m_strikeThroughColor,STYLE_STRIKE_THROUGH);
		setVertex(m_vertexCount+3, x1, y0, m_strikeThroughColor,STYLE_STRIKE_THROUGH);

		m_indexBuffer[m_indexCount + 0] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 1] = m_vertexCount+1;
		m_indexBuffer[m_indexCount + 2] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 3] = m_vertexCount+0;
		m_indexBuffer[m_indexCount + 4] = m_vertexCount+2;
		m_indexBuffer[m_indexCount + 5] = m_vertexCount+3;
		m_vertexCount += 4;
		m_indexCount += 6;
	}
	

	//handle glyph
	float x0_precise = m_penX + (_glyphInfo.m_offset_x);
	float x0 = ( x0_precise);
	float y0 = ( m_penY + (_glyphInfo.m_offset_y));
	float x1 = ( x0 + _glyphInfo.m_width );
	float y1 = ( y0 + _glyphInfo.m_height );
		
	m_fontManager->getAtlas()->packUV(_glyphInfo.m_regionIndex, (uint8_t*)m_vertexBuffer, sizeof(TextVertex) *m_vertexCount + offsetof(TextVertex, u), sizeof(TextVertex));

	setVertex(m_vertexCount+0, x0, y0, m_textColor);
	setVertex(m_vertexCount+1, x0, y1, m_textColor);
	setVertex(m_vertexCount+2, x1, y1, m_textColor);
	setVertex(m_vertexCount+3, x1, y0, m_textColor);

	m_indexBuffer[m_indexCount + 0] = m_vertexCount+0;
	m_indexBuffer[m_indexCount + 1] = m_vertexCount+1;
	m_indexBuffer[m_indexCount + 2] = m_vertexCount+2;
	m_indexBuffer[m_indexCount + 3] = m_vertexCount+0;
	m_indexBuffer[m_indexCount + 4] = m_vertexCount+2;
	m_indexBuffer[m_indexCount + 5] = m_vertexCount+3;
	m_vertexCount += 4;
	m_indexCount += 6;
	
	//TODO see what to do when doing subpixel rendering
	m_penX += _glyphInfo.m_advance_x;
}

void TextBuffer::verticalCenterLastLine(float _dy, float _top, float _bottom)
{		
	for( uint32_t ii=m_lineStartIndex; ii < m_vertexCount; ii+=4 )
    {	
		if( m_styleBuffer[ii] == STYLE_BACKGROUND)
		{
			m_vertexBuffer[ii+0].y = _top;
			m_vertexBuffer[ii+1].y = _bottom;
			m_vertexBuffer[ii+2].y = _bottom;
			m_vertexBuffer[ii+3].y = _top;
		}else{
			m_vertexBuffer[ii+0].y += _dy;
			m_vertexBuffer[ii+1].y += _dy;
			m_vertexBuffer[ii+2].y += _dy;
			m_vertexBuffer[ii+3].y += _dy;
		}
    }
}

// ****************************************************************

TextBufferManager::TextBufferManager(FontManager* _fontManager):m_fontManager(_fontManager), m_textBufferHandles(MAX_TEXT_BUFFER_COUNT)
{
	m_textBuffers = new BufferCache[MAX_TEXT_BUFFER_COUNT];
}

TextBufferManager::~TextBufferManager()
{
	assert(m_textBufferHandles.getNumHandles() == 0 && "All the text buffers must be destroyed before destroying the manager");
	delete[] m_textBuffers;

	bgfx::destroyUniform(m_u_texColor);
	bgfx::destroyUniform(m_u_inverse_gamma);

	bgfx::destroyProgram(m_basicProgram);	
	bgfx::destroyProgram(m_distanceProgram);	
	bgfx::destroyProgram(m_distanceSubpixelProgram);	
}

void TextBufferManager::init(const char* _shaderPath)
{
	m_vertexDecl.begin();
	m_vertexDecl.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float);
	m_vertexDecl.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Int16, true);
	m_vertexDecl.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true);
	m_vertexDecl.end();

	m_u_texColor = bgfx::createUniform("u_texColor", bgfx::UniformType::Uniform1iv);
	m_u_inverse_gamma = bgfx::createUniform("u_inverse_gamma", bgfx::UniformType::Uniform1f);

	const bgfx::Memory* mem;
	mem = loadShader(_shaderPath, "vs_font_basic");
	bgfx::VertexShaderHandle vsh = bgfx::createVertexShader(mem);
	mem = loadShader(_shaderPath, "fs_font_basic");
	bgfx::FragmentShaderHandle fsh = bgfx::createFragmentShader(mem);
	m_basicProgram = bgfx::createProgram(vsh, fsh);
	bgfx::destroyVertexShader(vsh);
	bgfx::destroyFragmentShader(fsh);	

	mem = loadShader(_shaderPath, "vs_font_distance_field");	
	vsh = bgfx::createVertexShader(mem);	
	mem = loadShader(_shaderPath, "fs_font_distance_field");
	fsh = bgfx::createFragmentShader(mem);
	m_distanceProgram = bgfx::createProgram(vsh, fsh);
	bgfx::destroyVertexShader(vsh);
	bgfx::destroyFragmentShader(fsh);
	
	mem = loadShader(_shaderPath, "vs_font_distance_field_subpixel");
	vsh = bgfx::createVertexShader(mem);		
	mem = loadShader(_shaderPath, "fs_font_distance_field_subpixel");
	fsh = bgfx::createFragmentShader(mem);
	m_distanceSubpixelProgram = bgfx::createProgram(vsh, fsh);
	bgfx::destroyVertexShader(vsh);
	bgfx::destroyFragmentShader(fsh);	
}

TextBufferHandle TextBufferManager::createTextBuffer(FontType _type, BufferType _bufferType)
{	
	uint16_t textIdx = m_textBufferHandles.alloc();
	BufferCache& bc = m_textBuffers[textIdx];
	
	bc.m_textBuffer = new TextBuffer(m_fontManager);	
	bc.m_fontType = _type;
	bc.m_bufferType = _bufferType;	
	bc.m_indexBufferHandle = bgfx::invalidHandle;
	bc.m_vertexBufferHandle = bgfx::invalidHandle;

	TextBufferHandle ret = {textIdx};
	return  ret;
}

void TextBufferManager::destroyTextBuffer(TextBufferHandle _handle)
{	
	assert( bgfx::invalidHandle != _handle.idx);
	
	BufferCache& bc = m_textBuffers[_handle.idx];
	m_textBufferHandles.free(_handle.idx);
	delete bc.m_textBuffer;
	bc.m_textBuffer = NULL;

	if(bc.m_vertexBufferHandle == bgfx::invalidHandle ) return;
	
	switch(bc.m_bufferType)
	{
	case STATIC:
		{
		bgfx::IndexBufferHandle ibh;
		bgfx::VertexBufferHandle vbh;
		ibh.idx = bc.m_indexBufferHandle;
		vbh.idx = bc.m_vertexBufferHandle;
		bgfx::destroyIndexBuffer(ibh);
		bgfx::destroyVertexBuffer(vbh);
		}

		break;
	case DYNAMIC:
		bgfx::DynamicIndexBufferHandle ibh;
		bgfx::DynamicVertexBufferHandle vbh;
		ibh.idx = bc.m_indexBufferHandle;
		vbh.idx = bc.m_vertexBufferHandle;
		bgfx::destroyDynamicIndexBuffer(ibh);
		bgfx::destroyDynamicVertexBuffer(vbh);
	
		break;
	case TRANSIENT: //naturally destroyed
		break;		
	}	
}

void TextBufferManager::submitTextBuffer(TextBufferHandle _handle, uint8_t _id, int32_t _depth)
{
	assert(bgfx::invalidHandle != _handle.idx);
	BufferCache& bc = m_textBuffers[_handle.idx];
	
	uint32_t indexSize = bc.m_textBuffer->getIndexCount() * bc.m_textBuffer->getIndexSize();
	uint32_t vertexSize = bc.m_textBuffer->getVertexCount() * bc.m_textBuffer->getVertexSize();
	const bgfx::Memory* mem;

	bgfx::setTexture(0, m_u_texColor, m_fontManager->getAtlas()->getTextureHandle());
	float inverse_gamme = 1.0f/2.2f;
	bgfx::setUniform(m_u_inverse_gamma, &inverse_gamme);
	
	switch (bc.m_fontType)
	{
	case FONT_TYPE_ALPHA:
		bgfx::setProgram(m_basicProgram);
		bgfx::setState( BGFX_STATE_RGB_WRITE | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA) );
		break;
	case FONT_TYPE_DISTANCE:
		bgfx::setProgram(m_distanceProgram);
		bgfx::setState( BGFX_STATE_RGB_WRITE | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA) );
		break;
	case FONT_TYPE_DISTANCE_SUBPIXEL:
		bgfx::setProgram(m_distanceSubpixelProgram);
		bgfx::setState( BGFX_STATE_RGB_WRITE |BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_INV_SRC_COLOR) , bc.m_textBuffer->getTextColor());
		break;	
	}	

	switch(bc.m_bufferType)
	{
		case STATIC:
		{
			bgfx::IndexBufferHandle ibh;
			bgfx::VertexBufferHandle vbh;

			if(bc.m_vertexBufferHandle == bgfx::invalidHandle)
			{
				mem = bgfx::alloc(indexSize);
				memcpy(mem->data, bc.m_textBuffer->getIndexBuffer(), indexSize);
				ibh = bgfx::createIndexBuffer(mem);

				mem = bgfx::alloc(vertexSize);
				memcpy(mem->data, bc.m_textBuffer->getVertexBuffer(), vertexSize);
				vbh = bgfx::createVertexBuffer(mem, m_vertexDecl);

				bc.m_indexBufferHandle = ibh.idx ;
				bc.m_vertexBufferHandle = vbh.idx;
			}else
			{
				ibh.idx = bc.m_indexBufferHandle;
				vbh.idx = bc.m_vertexBufferHandle;
			}
			bgfx::setVertexBuffer(vbh,  bc.m_textBuffer->getVertexCount());
			bgfx::setIndexBuffer(ibh, bc.m_textBuffer->getIndexCount());
		}break;
		case DYNAMIC:
		{
			bgfx::DynamicIndexBufferHandle ibh;
			bgfx::DynamicVertexBufferHandle vbh;

			if(bc.m_vertexBufferHandle == bgfx::invalidHandle)
			{
				mem = bgfx::alloc(indexSize);
				memcpy(mem->data, bc.m_textBuffer->getIndexBuffer(), indexSize);
				ibh = bgfx::createDynamicIndexBuffer(mem);

				mem = bgfx::alloc(vertexSize);
				memcpy(mem->data, bc.m_textBuffer->getVertexBuffer(), vertexSize);
				vbh = bgfx::createDynamicVertexBuffer(mem, m_vertexDecl);

				bc.m_indexBufferHandle = ibh.idx ;
				bc.m_vertexBufferHandle = vbh.idx;
			}else
			{
				ibh.idx = bc.m_indexBufferHandle;
				vbh.idx = bc.m_vertexBufferHandle;				
								
				mem = bgfx::alloc(indexSize);
				memcpy(mem->data, bc.m_textBuffer->getIndexBuffer(), indexSize);
				bgfx::updateDynamicIndexBuffer(ibh, mem);

				mem = bgfx::alloc(vertexSize);
				memcpy(mem->data, bc.m_textBuffer->getVertexBuffer(), vertexSize);
				bgfx::updateDynamicVertexBuffer(vbh, mem);				
			}
			bgfx::setVertexBuffer(vbh,  bc.m_textBuffer->getVertexCount());
			bgfx::setIndexBuffer(ibh, bc.m_textBuffer->getIndexCount());
			
		}break;
		case TRANSIENT:
		{
			bgfx::TransientIndexBuffer tib;
			bgfx::TransientVertexBuffer tvb;
			bgfx::allocTransientIndexBuffer(&tib, bc.m_textBuffer->getIndexCount());
			bgfx::allocTransientVertexBuffer(&tvb, bc.m_textBuffer->getVertexCount(), m_vertexDecl);
			memcpy(tib.data, bc.m_textBuffer->getIndexBuffer(), indexSize);
			memcpy(tvb.data, bc.m_textBuffer->getVertexBuffer(), vertexSize);
			bgfx::setVertexBuffer(&tvb,  bc.m_textBuffer->getVertexCount());
			bgfx::setIndexBuffer(&tib, bc.m_textBuffer->getIndexCount());
		}break;	
	}

	bgfx::submit(_id, _depth);
}

void TextBufferManager::submitTextBufferMask(TextBufferHandle /*_handle*/, uint32_t /*_viewMask*/, int32_t /*_depth*/)
{
	//TODO
	assert(false);
}

void TextBufferManager::setStyle(TextBufferHandle _handle, uint32_t _flags ) 
{ 
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setStyle(_flags); 
}

void TextBufferManager::setTextColor(TextBufferHandle _handle, uint32_t _rgba ) 
{ 
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setTextColor(_rgba); 
}

void TextBufferManager::setBackgroundColor(TextBufferHandle _handle, uint32_t _rgba ) 
{ 
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setBackgroundColor(_rgba); 
}

void TextBufferManager::setOverlineColor(TextBufferHandle _handle, uint32_t _rgba ) 
{ 
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setOverlineColor(_rgba); 
}

void TextBufferManager::setUnderlineColor(TextBufferHandle _handle, uint32_t _rgba ) 
{
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setUnderlineColor(_rgba); 
}

void TextBufferManager::setStrikeThroughColor(TextBufferHandle _handle, uint32_t _rgba ) 
{ 
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setStrikeThroughColor(_rgba); 
}
	
void TextBufferManager::setPenPosition(TextBufferHandle _handle, float _x, float _y) 
{
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	 bc.m_textBuffer->setPenPosition(_x,_y); 
}

void TextBufferManager::appendText(TextBufferHandle _handle, FontHandle _fontHandle, const char * _string)
{
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	bc.m_textBuffer->appendText(_fontHandle, _string);
}

void TextBufferManager::appendText(TextBufferHandle _handle, FontHandle _fontHandle, const wchar_t * _string)
{
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	bc.m_textBuffer->appendText(_fontHandle, _string);
}

void TextBufferManager::clearTextBuffer(TextBufferHandle _handle)
{
	assert( _handle.idx != bgfx::invalidHandle);
	BufferCache& bc = m_textBuffers[_handle.idx];
	bc.m_textBuffer->clearTextBuffer();
}
