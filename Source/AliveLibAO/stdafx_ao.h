// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifndef TETHYS_SATURN // SATURN: no ios_base::Init in every TU (see pch_shared.h)
#include <iostream>
#endif
#include <memory>
#include <map>
#include "../AliveLibCommon/logger.hpp"
#include "../AliveLibCommon/pch_shared.h"
#include "../AliveLibCommon/relive_config.h"
