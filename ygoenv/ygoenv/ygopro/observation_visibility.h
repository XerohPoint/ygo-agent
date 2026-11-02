#pragma once
#include "ygopro-core/common.h"
namespace ygopro {
// Knowledge is scoped to the viewing player. Search permission grants access
// only to the owner's offered candidates, never to an opponent's hidden zone.
inline bool observation_card_hidden(unsigned location, unsigned position,
                                    bool opponent, bool revealed, bool own_search) {
  if ((location & LOCATION_OVERLAY) || revealed) return false;
  if (location == LOCATION_DECK) return opponent || !own_search;
  return opponent && (location == LOCATION_HAND || location == LOCATION_EXTRA ||
                       (position & POS_FACEDOWN));
}
}
