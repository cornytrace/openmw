#include "loadgrup.hpp"

#include "esmreader.hpp"
#include "esmwriter.hpp"
#include "defs.hpp"

namespace ESM 
{
	unsigned int Group::sRecordId = REC_GRUP;

	void Group::load(ESMReader & esm, bool & isDeleted)
	{
		esm.getT(groupSize);
		esm.getT(label);
		esm.getT(groupType);
		esm.getT(stamp);
	}

	void Group::save(ESMWriter &esm, bool isDeleted = false) const
	{

	}

	void Group::blank()
	{

	}
}

