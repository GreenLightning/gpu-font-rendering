// Note: See "main.cpp" for headers.
// This file was extracted to improve the organization of the code,
// but it is still compiled in the "main.cpp" translation unit,
// because both files have mostly the same dependencies (OpenGL, GLM, FreeType).

class Font {
	struct Glyph {
		FT_UInt index;
		int32_t bufferIndex;

		// Important glyph metrics in font units.
		FT_Pos width, height;
		FT_Pos bearingX;
		FT_Pos bearingY;
		FT_Pos advance;
	};

	struct BufferGlyph {
		int32_t start, count; // range of bezier curves belonging to this glyph
	};

	struct BufferCurve {
		float x0, y0, x1, y1, x2, y2;
	};

	struct BufferVertex {
		float   x, y, u, v;
		int32_t bufferIndex;
	};

public:
	static FT_Face loadFace(FT_Library library, const std::string& filename, std::string& error) {
		FT_Face face;

		FT_Error ftError = FT_New_Face(library, filename.c_str(), 0, &face);
		if (ftError) {
			const char* ftErrorStr = FT_Error_String(ftError);
			if (ftErrorStr) {
				error = std::string(ftErrorStr);
			} else {
				// Fallback in case FT_Error_String returns NULL (e.g. if there
				// was an error or FT was compiled without error strings).
				std::stringstream stream;
				stream << "Error " << ftError;
				error = stream.str();
			}
			return face;
		}

		if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
			error = "non-scalable fonts are not supported";
			return face;
		}

		return face;
	}

	Font(FT_Face face) : face(face) {
		emSize = face->units_per_EM;

		glGenVertexArrays(1, &vao);

		glGenBuffers(1, &vbo);
		glGenBuffers(1, &ebo);

		glGenTextures(1, &glyphTexture);
		glGenTextures(1, &curveTexture);

		glGenBuffers(1, &glyphBuffer);
		glGenBuffers(1, &curveBuffer);

		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(BufferVertex), (void*)offsetof(BufferVertex, x));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(BufferVertex), (void*)offsetof(BufferVertex, u));
		glEnableVertexAttribArray(2);
		glVertexAttribIPointer(2, 1, GL_INT, sizeof(BufferVertex), (void*)offsetof(BufferVertex, bufferIndex));
		glBindVertexArray(0);

		{
			uint32_t charcode = 0;
			FT_UInt glyphIndex = 0;
			FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
			if (error) {
				std::cerr << "[font] error while loading undefined glyph: " << error << std::endl;
				// Continue, because we always want an entry for the undefined glyph in our glyphs map!
			}

			buildGlyph(charcode, glyphIndex);
		}

		for (uint32_t charcode = 32; charcode < 128; charcode++) {
			FT_UInt glyphIndex = FT_Get_Char_Index(face, charcode);
			if (!glyphIndex) continue;

			FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
			if (error) {
				std::cerr << "[font] error while loading glyph for character " << charcode << ": " << error << std::endl;
				continue;
			}

			buildGlyph(charcode, glyphIndex);
		}

		uploadBuffers();

		glBindTexture(GL_TEXTURE_BUFFER, glyphTexture);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32I, glyphBuffer);
		glBindTexture(GL_TEXTURE_BUFFER, 0);

		glBindTexture(GL_TEXTURE_BUFFER, curveTexture);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, curveBuffer);
		glBindTexture(GL_TEXTURE_BUFFER, 0);
	}

	~Font() {
		glDeleteVertexArrays(1, &vao);

		glDeleteBuffers(1, &vbo);
		glDeleteBuffers(1, &ebo);

		glDeleteTextures(1, &glyphTexture);
		glDeleteTextures(1, &curveTexture);

		glDeleteBuffers(1, &glyphBuffer);
		glDeleteBuffers(1, &curveBuffer);
	}

public:
	void prepareGlyphsForText(const std::string& text) {
		bool changed = false;

		for (const char* textIt = text.c_str(); *textIt != '\0'; ) {
			uint32_t charcode = decodeCharcode(&textIt);

			if(glyphs.count(charcode) != 0) continue;

			FT_UInt glyphIndex = FT_Get_Char_Index(face, charcode);
			if (!glyphIndex) continue;

			FT_Error error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
			if (error) {
				std::cerr << "[font] error while loading glyph for character " << charcode << ": " << error << std::endl;
				continue;
			}

			buildGlyph(charcode, glyphIndex);
			changed = true;
		}

		if (changed) {
			// Reupload the full buffer contents. To make this even more
			// dynamic, the buffers could be overallocated and only the added
			// data could be uploaded.
			uploadBuffers();
		}
	}

private:
	void uploadBuffers() {
		glBindBuffer(GL_TEXTURE_BUFFER, glyphBuffer);
		glBufferData(GL_TEXTURE_BUFFER, sizeof(BufferGlyph) * bufferGlyphs.size(), bufferGlyphs.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);

		glBindBuffer(GL_TEXTURE_BUFFER, curveBuffer);
		glBufferData(GL_TEXTURE_BUFFER, sizeof(BufferCurve) * bufferCurves.size(), bufferCurves.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);
	}

	// Decodes the first Unicode code point from the null-terminated UTF-8 string *text and advances *text to point at the next code point.
	// If the encoding is invalid, advances *text by one byte and returns 0.
	// *text should not be empty, because it will be advanced past the null terminator.
	uint32_t decodeCharcode(const char** text) {
		uint8_t first = static_cast<uint8_t>((*text)[0]);

		// Fast-path for ASCII.
		if (first < 128) {
			(*text)++;
			return static_cast<uint32_t>(first);
		}

		// This could probably be optimized a bit.
		uint32_t result;
		int size;
		if ((first & 0xE0) == 0xC0) { // 110xxxxx
			result = first & 0x1F;
			size = 2;
		} else if ((first & 0xF0) == 0xE0) { // 1110xxxx
			result = first & 0x0F;
			size = 3;
		} else if ((first & 0xF8) == 0xF0) { // 11110xxx
			result = first & 0x07;
			size = 4;
		} else {
			// Invalid encoding.
			(*text)++;
			return 0;
		}

		for (int i = 1; i < size; i++) {
			uint8_t value = static_cast<uint8_t>((*text)[i]);
			// Invalid encoding (also catches a null terminator in the middle of a code point).
			if ((value & 0xC0) != 0x80) { // 10xxxxxx
				(*text)++;
				return 0;
			}
			result = (result << 6) | (value & 0x3F);
		}

		(*text) += size;
		return result;
	}

	void buildGlyph(uint32_t charcode, FT_UInt glyphIndex) {
		BufferGlyph bufferGlyph;
		bufferGlyph.start = static_cast<int32_t>(bufferCurves.size());

		short start = 0;
		for (int i = 0; i < face->glyph->outline.n_contours; i++) {
			// Note: The end indices in face->glyph->outline.contours are inclusive.
			convertContour(bufferCurves, &face->glyph->outline, start, face->glyph->outline.contours[i], emSize);
			start = face->glyph->outline.contours[i]+1;
		}

		bufferGlyph.count = static_cast<int32_t>(bufferCurves.size()) - bufferGlyph.start;

		int32_t bufferIndex = static_cast<int32_t>(bufferGlyphs.size());
		bufferGlyphs.push_back(bufferGlyph);

		Glyph glyph;
		glyph.index = glyphIndex;
		glyph.bufferIndex = bufferIndex;
		glyph.width = face->glyph->metrics.width;
		glyph.height = face->glyph->metrics.height;
		glyph.bearingX = face->glyph->metrics.horiBearingX;
		glyph.bearingY = face->glyph->metrics.horiBearingY;
		glyph.advance = face->glyph->metrics.horiAdvance;
		glyphs[charcode] = glyph;
	}

	// This function takes a single contour (defined by firstIndex and
	// lastIndex, both inclusive) from outline and converts it into individual
	// quadratic bezier curves, which are added to the curves vector.
	void convertContour(std::vector<BufferCurve>& curves, const FT_Outline* outline, short firstIndex, short lastIndex, float emSize) {
		// TrueType fonts only contain quadratic bezier curves.
		// OpenType fonts may contain outline data in TrueType format
		// or in Compact Font Format, which also allows cubic beziers.

		// Each point in the contour has a tag specifying whether the point is
		// on the curve or not (off-curve points are control points).
		// A quadratic bezier curve is formed by three points: on - off - on.
		// A contour is a list of points that define a closed bezier spline (the
		// start point of a curve is the end point of the previous curve and the
		// end point of the last curve is the start point of the first curve).

		// Two consecutive points with the same type imply a virtual point of
		// the opposite type at their center. For example a line segment is
		// encoded as: on - on. Adding a virtual control point at the center
		// of the two endpoints (on - (off) - on) creates a quadratic bezier
		// curve representing the same line segment. Similarly, two consecutive
		// off points imply a virtual on point, so the sequence
		// on - off - off - on is expanded to on - off - (on) - off - on and
		// defines a chain of two bezier curves sharing the virtual point as one
		// of their end points.

		if (firstIndex == lastIndex) return;

		short dIndex = 1;
		if (outline->flags & FT_OUTLINE_REVERSE_FILL) {
			short tmpIndex = lastIndex;
			lastIndex = firstIndex;
			firstIndex = tmpIndex;
			dIndex = -1;
		}

		auto convert = [emSize](const FT_Vector& v) {
			return glm::vec2(
				(float)v.x / emSize,
				(float)v.y / emSize
			);
		};

		auto makeMidpoint = [](const glm::vec2& a, const glm::vec2& b) {
			return 0.5f * (a + b);
		};

		auto makeCurve = [](const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) {
			BufferCurve result;
			result.x0 = p0.x;
			result.y0 = p0.y;
			result.x1 = p1.x;
			result.y1 = p1.y;
			result.x2 = p2.x;
			result.y2 = p2.y;
			return result;
		};

		// Find a point that is on the curve and remove it from the list.
		glm::vec2 first;
		bool firstOnCurve = (outline->tags[firstIndex] & FT_CURVE_TAG_ON);
		if (firstOnCurve) {
			first = convert(outline->points[firstIndex]);
			firstIndex += dIndex;
		} else {
			bool lastOnCurve = (outline->tags[lastIndex] & FT_CURVE_TAG_ON);
			if (lastOnCurve) {
				first = convert(outline->points[lastIndex]);
				lastIndex -= dIndex;
			} else {
				first = makeMidpoint(convert(outline->points[firstIndex]), convert(outline->points[lastIndex]));
				// This is a virtual point, so we don't have to remove it.
			}
		}

		glm::vec2 start = first;
		glm::vec2 control = first;
		glm::vec2 previous = first;
		char previousTag = FT_CURVE_TAG_ON;
		for (short index = firstIndex; index != lastIndex + dIndex; index += dIndex) {
			glm::vec2 current = convert(outline->points[index]);
			char currentTag = FT_CURVE_TAG(outline->tags[index]);
			if (currentTag == FT_CURVE_TAG_CUBIC) {
				// No-op, wait for more points.
				control = previous;
			} else if (currentTag == FT_CURVE_TAG_ON) {
				if (previousTag == FT_CURVE_TAG_CUBIC) {
					glm::vec2& b0 = start;
					glm::vec2& b1 = control;
					glm::vec2& b2 = previous;
					glm::vec2& b3 = current;

					glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
					glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

					glm::vec2 d = makeMidpoint(c0, c1);

					curves.push_back(makeCurve(b0, c0, d));
					curves.push_back(makeCurve(d, c1, b3));
				} else if (previousTag == FT_CURVE_TAG_ON) {
					// Linear segment.
					curves.push_back(makeCurve(previous, makeMidpoint(previous, current), current));
				} else {
					// Regular bezier curve.
					curves.push_back(makeCurve(start, previous, current));
				}
				start = current;
				control = current;
			} else /* currentTag == FT_CURVE_TAG_CONIC */ {
				if (previousTag == FT_CURVE_TAG_ON) {
					// No-op, wait for third point.
				} else {
					// Create virtual on point.
					glm::vec2 mid = makeMidpoint(previous, current);
					curves.push_back(makeCurve(start, previous, mid));
					start = mid;
					control = mid;
				}
			}
			previous = current;
			previousTag = currentTag;
		}

		// Close the contour.
		if (previousTag == FT_CURVE_TAG_CUBIC) {
			glm::vec2& b0 = start;
			glm::vec2& b1 = control;
			glm::vec2& b2 = previous;
			glm::vec2& b3 = first;

			glm::vec2 c0 = b0 + 0.75f * (b1 - b0);
			glm::vec2 c1 = b3 + 0.75f * (b2 - b3);

			glm::vec2 d = makeMidpoint(c0, c1);

			curves.push_back(makeCurve(b0, c0, d));
			curves.push_back(makeCurve(d, c1, b3));

		} else if (previousTag == FT_CURVE_TAG_ON) {
			// Linear segment.
			curves.push_back(makeCurve(previous, makeMidpoint(previous, first), first));
		} else {
			curves.push_back(makeCurve(start, previous, first));
		}
	}

public:
	void drawSetup() {
		GLint location;

		location = glGetUniformLocation(program, "glyphs");
		glUniform1i(location, 0);
		location = glGetUniformLocation(program, "curves");
		glUniform1i(location, 1);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_BUFFER, glyphTexture);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_BUFFER, curveTexture);

		glActiveTexture(GL_TEXTURE0);
	}

	void draw(float x, float y, const std::string& text) {
		glBindVertexArray(vao);

		std::vector<BufferVertex> vertices;
		std::vector<int32_t> indices;

		FT_UInt previous = 0;
		for (const char* textIt = text.c_str(); *textIt != '\0'; ) {
			uint32_t charcode = decodeCharcode(&textIt);

			auto glyphIt = glyphs.find(charcode);
			Glyph& glyph = (glyphIt == glyphs.end()) ? glyphs[0] : glyphIt->second;

			if (previous != 0 && glyph.index != 0) {
				FT_Vector kerning;
				FT_Error error = FT_Get_Kerning(face, previous, glyph.index, FT_KERNING_UNSCALED, &kerning);
				if (!error) {
					x += (float)kerning.x / emSize * worldSize;
				}
			}

			FT_Pos d = (FT_Pos) (emSize * dilation);

			float u0 = (float)(glyph.bearingX-d) / emSize;
			float v0 = (float)(glyph.bearingY-glyph.height-d) / emSize;
			float u1 = (float)(glyph.bearingX+glyph.width+d) / emSize;
			float v1 = (float)(glyph.bearingY+d) / emSize;

			float x0 = x + u0 * worldSize;
			float y0 = y + v0 * worldSize;
			float x1 = x + u1 * worldSize;
			float y1 = y + v1 * worldSize;

			int32_t base = static_cast<int32_t>(vertices.size());
			vertices.push_back(BufferVertex{x0, y0, u0, v0, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x1, y0, u1, v0, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x1, y1, u1, v1, glyph.bufferIndex});
			vertices.push_back(BufferVertex{x0, y1, u0, v1, glyph.bufferIndex});
			indices.insert(indices.end(), { base, base+1, base+2, base+2, base+3, base });

			x += (float)glyph.advance / emSize * worldSize;
			previous = glyph.index;
		}

		glBufferData(GL_ARRAY_BUFFER, sizeof(BufferVertex) * vertices.size(), vertices.data(), GL_STREAM_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(int32_t) * indices.size(), indices.data(), GL_STREAM_DRAW);

		glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);

		glBindVertexArray(0);
	}

private:
	FT_Face face;
	float emSize;

	GLuint vao, vbo, ebo;
	GLuint glyphTexture, curveTexture;
	GLuint glyphBuffer, curveBuffer;

	std::vector<BufferGlyph> bufferGlyphs;
	std::vector<BufferCurve> bufferCurves;
	std::unordered_map<uint32_t, Glyph> glyphs;

public:
	GLuint program   = 0;
	float  dilation  = 0;
	float  worldSize = 1;
};
