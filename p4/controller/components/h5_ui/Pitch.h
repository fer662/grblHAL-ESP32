#ifndef PITCH_H
#define PITCH_H

typedef enum {
  PITCH_TYPE_MM_PER_TURN = 0,
  PITCH_TYPE_TPI = 1,
  PITCH_TYPE_INCHES_PER_TURN = 2,
} PitchType;

long convertMmToDupr(float mm);
long convertMmPerTurnToDupr(float mmPerTurn);
long convertTpiToDupr(float tpi);
float convertDuprToMmPerTurn(long dupr);
float convertDuprToTpi(long dupr);
float convertDuprToInchesPerTurn(long dupr);
long convertInchesPerTurnToDupr(float inchesPerTurn);

#endif // PITCH_H
