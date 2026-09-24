import warehouseJson from './map/warehouse.json';
import type { MapDef } from './map/schema';

export const MAPS: Record<string, MapDef> = {
  [warehouseJson.name]: warehouseJson as MapDef,
};

export * from './constants';
export * from './protocol';
export * from './weapons';
export * from './physics/aabb';
export * from './physics/raycast';
export * from './sim/movement';
export * from './sim/recoil';
export * from './sim/spread';
export * from './nav/grid';
export * from './nav/astar';
export * from './map/schema';
