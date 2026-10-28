
#include "LasLoader/LasLoader.h"

#include "laszip/laszip_api.h"

#include <mutex>

static bool recomputeBounds = false;
static mutex boundsMutex;
static unordered_map<string, pair<Vector3, Vector3>> boundsCache;

void setRecomputeBounds(bool enabled) {
	recomputeBounds = enabled;
}

// Bounds of every point in the file, padded by one scale unit on each side so
// a point on the edge still lands inside the box after the chunker re-quantises
// it. Returns false (bounds untouched) when the file has no readable points.
static bool computePointBounds(laszip_POINTER reader, int64_t numPoints, Vector3 scale, Vector3& min, Vector3& max) {
	Vector3 pmin = { Infinity, Infinity, Infinity };
	Vector3 pmax = { -Infinity, -Infinity, -Infinity };
	double coordinates[3];
	int64_t numRead = 0;

	for (int64_t i = 0; i < numPoints; i++) {
		if (laszip_read_point(reader)) break;
		laszip_get_coordinates(reader, coordinates);

		pmin.x = std::min(pmin.x, coordinates[0]);
		pmin.y = std::min(pmin.y, coordinates[1]);
		pmin.z = std::min(pmin.z, coordinates[2]);
		pmax.x = std::max(pmax.x, coordinates[0]);
		pmax.y = std::max(pmax.y, coordinates[1]);
		pmax.z = std::max(pmax.z, coordinates[2]);
		numRead++;
	}

	if (numRead == 0) return false;

	min = pmin - scale;
	max = pmax + scale;

	return true;
}


LasTypeInfo lasTypeInfo(int typeID) {

	unordered_map<int, AttributeType> mapping = {
		{0, AttributeType::UNDEFINED},
		{1, AttributeType::UINT8},
		{2, AttributeType::INT8},
		{3, AttributeType::UINT16},
		{4, AttributeType::INT16},
		{5, AttributeType::UINT32},
		{6, AttributeType::INT32},
		{7, AttributeType::UINT64},
		{8, AttributeType::INT64},
		{9, AttributeType::FLOAT},
		{10, AttributeType::DOUBLE},

		{11, AttributeType::UINT8},
		{12, AttributeType::INT8},
		{13, AttributeType::UINT16},
		{14, AttributeType::INT16},
		{15, AttributeType::UINT32},
		{16, AttributeType::INT32},
		{17, AttributeType::UINT64},
		{18, AttributeType::INT64},
		{19, AttributeType::FLOAT},
		{20, AttributeType::DOUBLE},

		{21, AttributeType::UINT8},
		{22, AttributeType::INT8},
		{23, AttributeType::UINT16},
		{24, AttributeType::INT16},
		{25, AttributeType::UINT32},
		{26, AttributeType::INT32},
		{27, AttributeType::UINT64},
		{28, AttributeType::INT64},
		{29, AttributeType::FLOAT},
		{30, AttributeType::DOUBLE},
	};

	if (mapping.find(typeID) != mapping.end()) {

		AttributeType type = mapping[typeID];

		int numElements = 0;
		if (typeID <= 10) {
			numElements = 1;
		} else if (typeID <= 20) {
			numElements = 2;
		} else if (typeID <= 30) {
			numElements = 3;
		}

		LasTypeInfo info;
		info.type = type;
		info.numElements = numElements;

		return info;
	} else {
		cout << "ERROR: unkown extra attribute type: " << typeID << endl;
		exit(123);
	}


}


LasHeader loadLasHeader(string path) {
	laszip_POINTER laszip_reader;
	laszip_header* header;
	laszip_point* point;

	laszip_BOOL request_reader = 1;
	laszip_BOOL is_compressed = iEndsWith(path, ".laz") ? 1 : 0;

	laszip_create(&laszip_reader);
	laszip_request_compatibility_mode(laszip_reader, request_reader);
	laszip_open_reader(laszip_reader, path.c_str(), &is_compressed);
	laszip_get_header_pointer(laszip_reader, &header);

	LasHeader result;

	result.min.x = header->min_x;
	result.min.y = header->min_y;
	result.min.z = header->min_z;

	result.max.x = header->max_x;
	result.max.y = header->max_y;
	result.max.z = header->max_z;

	result.scale.x = header->x_scale_factor;
	result.scale.y = header->y_scale_factor;
	result.scale.z = header->z_scale_factor;

	result.offset.x = header->x_offset;
	result.offset.y = header->y_offset;
	result.offset.z = header->z_offset;

	result.numPoints = std::max(header->extended_number_of_point_records, uint64_t(header->number_of_point_records));

	result.pointDataFormat = header->point_data_format;

	int numVlrs = header->number_of_variable_length_records;
	for (int i = 0; i < numVlrs; i++) {
		auto laszip_vlr = header->vlrs[i];

		VLR vlr;

		vlr.recordID = laszip_vlr.record_id;
		vlr.recordLengthAfterHeader = laszip_vlr.record_length_after_header;
		vlr.data.resize(vlr.recordLengthAfterHeader);

		memcpy(vlr.data.data(), laszip_vlr.data, vlr.recordLengthAfterHeader);

		result.vlrs.push_back(vlr);
	}

	if (recomputeBounds) {
		bool cached = false;
		{
			lock_guard<mutex> lock(boundsMutex);
			auto it = boundsCache.find(path);
			if (it != boundsCache.end()) {
				result.min = it->second.first;
				result.max = it->second.second;
				cached = true;
			}
		}

		Vector3 min = result.min;
		Vector3 max = result.max;
		if (!cached && computePointBounds(laszip_reader, result.numPoints, result.scale, min, max)) {
			cout << "recomputed bounds of " << path << endl;
			cout << "	header min: " << result.min.toString() << ", max: " << result.max.toString() << endl;
			cout << "	points min: " << min.toString() << ", max: " << max.toString() << endl;

			result.min = min;
			result.max = max;

			lock_guard<mutex> lock(boundsMutex);
			boundsCache[path] = { min, max };
		}
	}




	laszip_close_reader(laszip_reader);
	laszip_destroy(laszip_reader);

	return result;
}