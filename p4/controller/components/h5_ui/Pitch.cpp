long convertMmToDupr(float mm) {
  return (long)(mm * 10000.0f); // Convert to DUPR units
}

long convertMmPerTurnToDupr(float mmPerTurn) {
  return convertMmToDupr(mmPerTurn); // Convert to DUPR units
}

long convertTpiToDupr(float tpi) {
  float mmPerTurn = 25.4f / tpi; // Convert TPI to mm per turn
  return convertMmPerTurnToDupr(mmPerTurn);
}

float convertDuprToMmPerTurn(long dupr) {
  return (dupr / 10000.0f); // Convert to mm per turn
}

float convertDuprToTpi(long dupr) {
  float mmPerTurn = convertDuprToMmPerTurn(dupr);
  return (25.4f / mmPerTurn); // Convert mm per turn to TPI
}

float convertDuprToInchesPerTurn(long dupr) {
  float mmPerTurn = convertDuprToMmPerTurn(dupr);
  return (mmPerTurn / 25.4f); // Convert mm per turn to inches per turn
}

long convertInchesPerTurnToDupr(float inchesPerTurn) {
  float mmPerTurn =
      inchesPerTurn * 25.4f; // Convert inches per turn to mm per turn
  return convertMmPerTurnToDupr(mmPerTurn);
}
