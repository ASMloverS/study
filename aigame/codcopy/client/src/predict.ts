import {
  MAPS,
  PLAYER_CROUCH_HEIGHT,
  SHOT_MAX_DISTANCE,
  type InputMsg,
  type PlayerSnap,
  type WeaponId,
  createMoveState,
  mapToObstacles,
  raycastBoxes,
  stepMovement,
  type MoveState,
  type AABB,
} from 'shared';

export class Predictor {
  readonly state: MoveState = createMoveState(0, 0, 0);
  prevX = 0;
  prevY = 0;
  prevZ = 0;
  prevH = 1.8;
  private readonly obstacles: AABB[] = mapToObstacles(MAPS.warehouse);
  private history: { input: InputMsg; weapon: WeaponId }[] = [];
  private lastWeapon: WeaponId = 'ar';

  step(input: InputMsg, weapon: WeaponId = this.lastWeapon): void {
    this.lastWeapon = weapon;
    this.prevX = this.state.x;
    this.prevY = this.state.y;
    this.prevZ = this.state.z;
    this.prevH = this.state.height;
    this.history.push({ input, weapon });
    if (this.history.length > 120) this.history.shift();
    stepMovement(this.state, input, this.obstacles, undefined, weapon);
  }

  raycastObstacles(ox: number, oy: number, oz: number, dx: number, dy: number, dz: number): number | null {
    const hit = raycastBoxes(ox, oy, oz, dx, dy, dz, SHOT_MAX_DISTANCE, this.obstacles);
    return hit ? hit.t : null;
  }

  reconcile(snap: PlayerSnap, ackSeq: number): void {
    this.history = this.history.filter((i) => i.input.seq > ackSeq);
    const s = this.state;
    const drifted =
      Math.abs(s.x - snap.x) > 0.02 ||
      Math.abs(s.y - snap.y) > 0.02 ||
      Math.abs(s.z - snap.z) > 0.02 ||
      Math.abs(s.height - snap.h) > 0.01;
    if (!drifted) return;
    this.prevX = snap.x;
    this.prevY = snap.y;
    this.prevZ = snap.z;
    this.prevH = snap.h;
    s.x = snap.x;
    s.y = snap.y;
    s.z = snap.z;
    s.vx = snap.vx;
    s.vy = snap.vy;
    s.vz = snap.vz;
    s.yaw = snap.yaw;
    s.pitch = snap.pitch;
    s.height = snap.h;
    s.sliding = snap.sl;
    s.crouching = snap.h === PLAYER_CROUCH_HEIGHT && !snap.sl;
    s.onGround = Math.abs(snap.vy) < 0.01;
    for (const { input, weapon } of this.history) stepMovement(s, input, this.obstacles, undefined, weapon);
  }
}
