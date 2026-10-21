// OpenStreetMap serves tiles up to zoom 19. Past that Leaflet scales the z19
// tiles up (softer, but fine for placing pillars and reading positions to the
// metre); 22 is about 4x closer than z19.
export const OSM_MAX_NATIVE_ZOOM = 19;
export const MAX_ZOOM = 22;
