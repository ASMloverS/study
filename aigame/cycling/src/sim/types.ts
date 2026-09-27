export type GearId = 0 | 1 | 2 | 3;

export interface RiderCommand {
  gear: GearId;
  cadDelta: number;
  cogDelta: number;
  steer: number;
}

export interface RiderType {
  label: string;
  ftp: number;
  maxEnergy: number;
  sprintDist: number;
  aggression: number;
}

export interface RiderState {
  id: number;
  name: string;
  isPlayer: boolean;
  type: RiderType;
  dist: number;
  lateral: number;
  speed: number;
  energy: number;
  gear: GearId;
  cog: number;
  collected: boolean[];
  boxCursor: number;
  cadTarget: number;
  power: number;
  powerSum: number;
  timeSum: number;
  finishTime: number | null;
  wanderTarget: number;
}

export interface ResultRow {
  id: number;
  name: string;
  time: number;
  dnf: boolean;
  avgPower: number;
}

export type Phase = 'countdown' | 'racing' | 'finished';
