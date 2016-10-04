#pragma once

#include "esmcommon.hpp"
#include "defs.hpp"
#include "cellref.hpp"
#include "cellid.hpp"

namespace ESM
{
	class ESMReader;
	class ESMWriter;

	struct Group
	{
		static unsigned int sRecordId;
		/// Return a string descriptor for this record type. Currently used for debugging / error logs only.
		static std::string getRecordType() { return "Group"; }

		unsigned long groupSize;
		NAME label;
		long groupType;
		unsigned long stamp;

		void load(ESMReader &esm, bool &isDeleted);
		void save(ESMWriter &esm, bool isDeleted = false) const;

		void blank();
	};
}
