#pragma once
#include "common.h"

// the things that live in the dreams, built out of tapered limbs and rounded heads
typedef enum {
    FIG_PENITENT,   // a tall hooded figure, arms hanging too long out of its sleeves
    FIG_KNEELER,    // the same, kneeling, hands pressed together
    FIG_CRAWLER,    // something pale on all fours, blind, mouth hanging open
    FIG_CLIMBER,    // the crawler, flat against a wall
    FIG_GARDENER,   // very tall and thin, and its head is a flower with an eye in it
    FIG_PRIEST,     // robed, arms raised, wearing the skull of a goat
    FIG_SLEEPER,    // someone lying on their back under a sheet
    FIG_SEATED,     // a hooded figure sitting at a table, hands flat on it
    FIG_COCOON,     // someone wrapped up and hung from the ceiling by a cord. the face presses through
    FIG_TALL,       // a thin man in a dark suit, much too tall, with a face like an egg
    FIG_MOTHER,     // a woman in a stained nightgown, head bent too far over, hair hanging over her face
    FIG_HAND,       // a forearm and hand coming up out of something, fingers slowly closing
    FIG_FACE,       // a face in a wall, eyes following you, mouth working
    FIG_SHEET,      // someone standing up under a mortuary sheet, a tag on one toe
    FIG_COUNT
} FigKind;

typedef struct Fig {
    FigKind kind;
    Vector3 pos;        // feet (climber: where its belly touches the wall)
    float yaw;          // radians; 0 faces -Z, same convention as the player
    float stride;       // walk cycle phase (advance it by distance travelled)
    Vector3 lookAt;     // what the head turns toward
    float look;         // 0..1 how far it turns
    float tilt;         // head tilt, radians
    float t;            // clock, for the small movements
    Vector3 wallN;      // climber: the wall it is on
    Color tint;         // 0 alpha = the default for the kind
    float scale;        // 0 = life size. limbs stay just as thin when it is scaled up
} Fig;

void figure_draw(const Fig *f);
