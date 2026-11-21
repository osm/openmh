#include "backends/clock/system_provider.h"
#include "client/diagnostics/storage_league_file.h"
#include "engine/game/league_names.h"
#include "engine/storage/league_file.h"
#include "interfaces/clock/backend.h"

enum mh_error_code
mh_league_current_date(unsigned long *output, struct mh_error *error_out)
{
	struct mh_clock_date date;
	enum mh_error_code status;

	status = mh_clock_source_date(
		mh_system_clock_source(), &date, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_league_date(
		date.year - 1900, date.month - 1, date.day, output, error_out);
}
