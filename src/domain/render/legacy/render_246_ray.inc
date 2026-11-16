/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static WallRay CastWallRayLegacy246(int x) {
    float cameraX = 2.0f * x / (float)renderW - 1.0f;
    cameraX+=perception.distortion*sinf((float)g_timeSec*2.7f+cameraX*4);
    float rayDirX = dirX + planeX * cameraX;
    float rayDirY = dirY + planeY * cameraX;

    int mapX = (int)posX, mapY = (int)posY;
    float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
    float sideDistX, sideDistY;
    int stepX, stepY, hit = 0, side = 0;

    if (rayDirX < 0) { stepX = -1; sideDistX = (posX - mapX) * deltaDistX; }
    else { stepX = 1; sideDistX = (mapX + 1.0f - posX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (posY - mapY) * deltaDistY; }
    else { stepY = 1; sideDistY = (mapY + 1.0f - posY) * deltaDistY; }

    while (hit == 0) {
        if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
        else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
        if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT) break;
        if (worldMap[mapY][mapX] > 0) hit = 1;
    }

    float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
    if (perpWallDist < 0.1f) perpWallDist = 0.1f;
    zBuffer[x] = perpWallDist;
    WallRay result = { rayDirX, rayDirY, mapX, mapY, side, perpWallDist, stepX, stepY };
    return result;
}
