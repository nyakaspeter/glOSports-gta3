/* game.c -- Grand Theft Auto III, Rockstar's Android build (libR1.so, War
 * Drum Studios, 2011; the library of 2015-03-05, md5 884abb8d): hooks and
 * fixes. There is no Vita loader of this build to take them from; they are
 * found from the library itself. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "android.h"
#include "so_util.h"
#include "util.h"

const char *game_so_name = "libR1.so";
const char *const game_required_files[] = {
	"libR1.so", "data/main.scm", NULL,
};

/* FileGetArchiveName answers no archive for the archive CFileMgr asks for
 * (0): the game then opens every file loose from the storage root (the OBB
 * and the APK's assets/ unpacked into game/) */
const JavaMethod game_java_methods[] = {
	COMMON_JAVA_METHODS,
	{ NULL, NULL },
};

/* The pad, straight to the WarGamepad layer the Java calls would feed. Type
 * 0 is an Xbox 360 pad (Handle360Controller), the GA36-MB's layout.
 *
 * In the game the D-pad is the left stick: walking on foot, steering in a
 * vehicle, instead of its buttons (down changed the camera). Menus and
 * pauses (CTimer's user and code pause) keep the stock D-pad. */
static uint8_t *timer_user_pause, *timer_code_pause;
static void *(*FindPlayerPed)(void);

static int dpad_walking(void)
{
	return !*timer_user_pause && !*timer_code_pause && FindPlayerPed();
}

static int WarGamepad_GetGamepadType(void)
{
	pad_update();
	return 0;
}

static int WarGamepad_GetGamepadButtons(void)
{
	int b = pad_buttons();
	return dpad_walking() ? b & ~0xf00 : b;
}

static SOFTFP float WarGamepad_GetGamepadAxis(int axis)
{
	float v = pad_axis(axis);
	if (axis < 2 && dpad_walking()) {
		int b = pad_buttons();
		int neg = axis ? 0x100 : 0x400, pos = axis ? 0x200 : 0x800;
		if (b & neg)
			v = -1.0f;
		if (b & pos)
			v = 1.0f;
	}
	return v;
}

/* Vice City's right-stick camera. GTA III's classic camera turns right-stick
 * movement into a first-person look (CCamera::CamControl sets the camera's
 * first-person flag); the Vice City mobile build changed that spot to feed
 * the stick into Mobile_AdjustCam instead, the offsets a touch swipe on the
 * screen gives (Vice City's CamControl: x = -LookAroundLeftRight / 180 *
 * m_PrefsLookSensitivity * 0.3, 0 while the stick rests). GTA III's on-foot
 * camera already turns by Mobile_AdjustCam (Process_FollowPed: x onto its
 * angle, y onto its height, -1..2.3), so the same is done here: CamControl's
 * two stick reads go to this function, which feeds Mobile_AdjustCam with
 * Vice City's numbers (its default sensitivity 0.5; the vertical, which
 * Vice City puts on its camera-angle setting, onto y) and answers 0, so the
 * first-person look never starts. Lock-on, aiming and movement are stock.
 *
 * In a vehicle Vice City's CCam::Process turns the camera by
 * Mobile_AdjustCam.x every frame (LookOffsetAngle, then clears it); GTA
 * III's does so only during a touch swipe (GTouchscreen +316), and nothing
 * feeds it the stick there. So that check goes and the stick is fed each
 * frame while the player is in a vehicle. */
static float *mobile_adjust_cam;
static int16_t (*CPad__LookAroundLeftRight)(void *pad);
static int16_t (*CPad__LookAroundUpDown)(void *pad);

static int stick_camera(void *pad)
{
	if (!mobile_adjust_cam)
		return 0;
	int lr = CPad__LookAroundLeftRight(pad), ud = CPad__LookAroundUpDown(pad);
	if (lr || ud) {
		mobile_adjust_cam[0] = -lr / 180.0f * 0.5f * 0.3f;
		/* LookAroundUpDown applies Invert Look; off pushes the view the
		 * way the stick goes */
		mobile_adjust_cam[1] = -ud / 180.0f * 0.5f * 0.5f;
	} else {
		mobile_adjust_cam[0] = 0.0f;
		mobile_adjust_cam[1] = 0.0f;
	}
	return 0;
}

static void *(*CPad__GetPad)(int n);
static void *(*FindPlayerVehicle)(void);

static void stick_camera_vehicle(void)
{
	if (!FindPlayerVehicle())
		return;
	int lr = CPad__LookAroundLeftRight(CPad__GetPad(0));
	mobile_adjust_cam[0] = -lr / 180.0f * 0.5f * 0.3f;
}

static int return_0(void)
{
	return 0;
}

/* RB locks on as in Vice City: the mobile CPlayerPed::ProcessPlayerWeapon
 * looks for a target only while the player fires (the touch game's auto
 * aim), and reads the target button only there, where holding it drops the
 * lock. So the game's one call (CPlayerPed::ProcessControl) goes through
 * this: while RB is held with a weapon that can aim (CWeaponInfo +80 bit
 * 6; not the first-person weapons 6-8, which keep their own RB), it finds
 * a target, points the gun at it and keeps the lock's timer (+1192) up;
 * the game itself then turns the camera to the target and fires at it.
 * RB is hidden from the game's own check meanwhile, and releasing it
 * clears the lock. A flick of the right stick (x past half, from inside
 * half in the pad's previous state, +46) moves the lock to the next
 * target on that side (the camera follows the target meanwhile), and so
 * do the triggers, as in Vice City (LT left, RT right). Targets are
 * picked by GTA SA's rules (below). */
static void (*CPlayerPed__ProcessPlayerWeapon)(char *ped, int16_t *pad);
static void (*CPlayerPed__FindWeaponLockOnTarget)(char *ped);
static void (*CPlayerPed__ClearWeaponTarget)(char *ped);
static int (*CPlayerPed__FindNextWeaponLockOnTarget)(char *ped, void *previous, int left);
static void (*CPed__SetPointGunAt)(char *ped, void *target);
static uint8_t *(*CWeaponInfo__GetWeaponInfo)(int type);

static char *the_camera;

/* Lock-on follows GTA SA's rules (CPlayerPed::FindWeaponLockOnTarget,
 * EvaluateTarget, FindNextWeaponLockOnTarget, EvaluateNeighbouringTarget,
 * FindTargetPriority, LOSBlockedBetweenPeds). This game scored every
 * candidate in weapon range in front of the player, through walls too, by
 * distance plus 5 x the angle off. SA scores a candidate twice: off the
 * player's facing, and off the camera's heading when it is in front of
 * the camera (CSprite::CalcScreenCoors); the better counts. A score is
 * 1 - 0.785 x the angle off (none past about 73 degrees), over the fourth
 * root of the distance, times the target's priority (0.8 for peds attacking the player (IsThisPedAttackingPlayer), 0.1
 * for the rest; SA's own also rates cops, gangs and its mission targets,
 * which this game's peds don't carry), and counts only with a clear line
 * from neck to neck (buildings and objects). Switching to the next
 * target measures from the camera: the nearest angle from the current
 * target on the asked side, within 50 degrees, in line of sight. The
 * game's own pre-check (OurPedCanSeeThisOne: in front of the player,
 * 40 m), which SA doesn't have, is answered yes. */
typedef struct { float x, y, z; } CVector;
static SOFTFP int (*CPlayerPed__DoesTargetHaveToBeBroken)(char *ped, CVector pos, void *weapon);
static SOFTFP float (*CGeneral__GetATanOfXY)(float x, float y);
static int (*CSprite__CalcScreenCoors)(const float *in, float *out, float *w, float *h, int far_clip);
static int (*CWorld__GetIsLineOfSightClear)(const CVector *a, const CVector *b, int buildings, int vehicles,
	int peds, int objects, int dummies, int ignore_see_through, int ignore_some_objects);

static int return_1(void)
{
	return 1;
}

/* in range and holdable by the weapon */
static int can_lock(char *ped, char *target, float max_dist, float *dist)
{
	float *p = (float *)(ped + 52), *t = (float *)(target + 52);
	float dx = t[0] - p[0], dy = t[1] - p[1], dz = t[2] - p[2];
	*dist = sqrtf(dx * dx + dy * dy + dz * dz);
	if (*dist > max_dist)
		return 0;
	int slot = *(int8_t *)(ped + 1184);
	return !CPlayerPed__DoesTargetHaveToBeBroken(ped, (CVector){ t[0], t[1], t[2] }, ped + 868 + slot * 24);
}

static float angle_off(float dx, float dy, float heading)
{
	float a = CGeneral__GetATanOfXY(dx, dy) - heading;
	while (a > 3.14159265f)
		a -= 6.28318531f;
	while (a < -3.14159265f)
		a += 6.28318531f;
	return a;
}

static float sa_score(float angle, float dist)
{
	float s = 1.0f - 0.785398f * fabsf(angle);
	if (s <= 0.0f)
		return 0.0f;
	return dist > 1.0f ? s / sqrtf(sqrtf(dist)) : s;
}

/* the best so far, if nothing blocks the line from neck to neck */
static void want(char *ped, char *target, char **best, float *best_score, float score)
{
	if (score <= *best_score)
		return;
	float *p = (float *)(ped + 52), *t = (float *)(target + 52);
	CVector from = { p[0], p[1], p[2] + 0.6f }, to = { t[0], t[1], t[2] + 0.6f };
	if (!CWorld__GetIsLineOfSightClear(&from, &to, 1, 0, 0, 1, 0, 1, 0))
		return;
	*best = target;
	*best_score = score;
}

/* heading: the player's facing (FindWeaponLockOnTarget) */
static SOFTFP void evaluate_target(char *ped, char *target, char **best, float *best_score,
	float max_dist, float heading, int priority)
{
	float dist;
	if (!can_lock(ped, target, max_dist, &dist))
		return;
	float *p = (float *)(ped + 52), *t = (float *)(target + 52), *cf = (float *)(the_camera + 20);
	float score = sa_score(angle_off(t[0] - p[0], t[1] - p[1], heading), dist);
	float screen[3], w, h;
	if (CSprite__CalcScreenCoors(t, screen, &w, &h, 1)) {
		float s = sa_score(angle_off(t[0] - p[0], t[1] - p[1], CGeneral__GetATanOfXY(cf[0], cf[1])), dist);
		if (s > score)
			score = s;
	}
	if (score > 0.0f)
		want(ped, target, best, best_score, score * (priority ? 0.8f : 0.1f));
}

/* the game's heading here is from the player; SA's is from the camera to
 * the current target */
static SOFTFP void evaluate_neighbouring_target(char *ped, char *target, char **best, float *best_score,
	float max_dist, float heading, int left)
{
	float dist;
	if (!can_lock(ped, target, max_dist, &dist))
		return;
	float *t = (float *)(target + 52), *cf = (float *)(the_camera + 20), *cp = (float *)(the_camera + 52);
	char *current = *(char **)(ped + 1188);
	float *c = current ? (float *)(current + 52) : NULL;
	float from_camera = c ? CGeneral__GetATanOfXY(c[0] - cp[0], c[1] - cp[1]) : CGeneral__GetATanOfXY(cf[0], cf[1]);
	float a = angle_off(t[0] - cp[0], t[1] - cp[1], from_camera);
	if (fabsf(a) >= 0.872665f || (left ? a <= 0.0f : a >= 0.0f))
		return;
	want(ped, target, best, best_score, -fabsf(a));
}

static void process_player_weapon(char *ped, int16_t *pad)
{
	static int locked;
	void **target = (void **)(ped + 1188);
	int slot = *(int8_t *)(ped + 1184);
	int type = *(int *)(ped + 868 + slot * 24);
	int held = pad[6] && !*((uint8_t *)pad + 223) && (type < 6 || type > 8) &&
	           (CWeaponInfo__GetWeaponInfo(type)[80] & 0x40);
	int16_t rb = pad[6];
	if (held) {
		if (!*target)
			CPlayerPed__FindWeaponLockOnTarget(ped);
		if (*target && locked) {
			int x = pad[2], old = pad[23];
			int lt = pad[5] && !pad[26], rt = pad[7] && !pad[28];
			if (lt || rt || (x <= -64 && old > -64) || (x >= 64 && old < 64))
				CPlayerPed__FindNextWeaponLockOnTarget(ped, *target, lt || (!rt && x < 0));
		}
		if (*target) {
			locked = 1;
			*(float *)(ped + 1192) = 0.6f;
			if (*(int *)(ped + 556) != 16) /* not mid-attack */
				CPed__SetPointGunAt(ped, *target);
		}
		pad[6] = 0;
	} else if (locked) {
		locked = 0;
		if (*target)
			CPlayerPed__ClearWeaponTarget(ped);
	}
	CPlayerPed__ProcessPlayerWeapon(ped, pad);
	pad[6] = rb;
}

/* a bl in the game's code reaches +-16 MB: a jump to the loader's function
 * from a page right after the library */
static uint8_t *veneers;
static size_t veneers_used;

static uintptr_t veneer(uintptr_t target)
{
	if (!veneers) {
		uintptr_t want = (so_base + so_span + 0xfffff) & ~0xfffffu;
		veneers = mmap((void *)want, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (veneers == MAP_FAILED)
			fatal_error("veneer page: no memory");
	}
	uint8_t *v = veneers + veneers_used;
	static const uint8_t ldr_pc[4] = { 0xdf, 0xf8, 0x00, 0xf0 }; /* ldr.w pc, [pc] */
	memcpy(v, ldr_pc, 4);
	*(uint32_t *)(v + 4) = target | 1;
	veneers_used += 8;
	__builtin___clear_cache((char *)v, (char *)v + 8);
	return (uintptr_t)v;
}

/* the bl at off (checked against its original bytes) to target */
static int patch_bl(uintptr_t off, const uint8_t orig[4], uintptr_t target)
{
	if (memcmp((void *)(so_base + off), orig, 4) != 0)
		return 0;
	uintptr_t v = veneer(target);
	int32_t d = (int32_t)(v - (so_base + off + 4));
	if (d < -(1 << 24) || d >= (1 << 24))
		return 0;
	uint32_t imm = ((uint32_t)d >> 1) & 0xffffff;
	uint32_t S = (imm >> 23) & 1, I1 = (imm >> 22) & 1, I2 = (imm >> 21) & 1;
	uint32_t J1 = (!I1) ^ S, J2 = (!I2) ^ S;
	uint16_t hw[2] = {
		(uint16_t)(0xf000 | (S << 10) | ((imm >> 11) & 0x3ff)),
		(uint16_t)(0xd000 | (J1 << 13) | (J2 << 11) | (imm & 0x7ff)),
	};
	so_patch(off, hw, 4);
	return 1;
}

/* The HUD is laid out like the touch controls: the radar is drawn in the
 * touchscreen's radar button (CRadar::TransformRadarPointToScreenSpace),
 * clock, money, health, weapon and wanted level in its summary button,
 * the help box in its tut button (CHud::Draw), each a rectangle of floats x, y, width, height at +4..+16.
 * The loader places them as GTA SA's Xbox 360 layout does on this screen
 * (Adjustable.cfg's radar: centre 76, 370 and radius 44 in 640x448; the
 * stats block measured in the game): the radar bottom left, with SA's size,
 * the stats top right and the help box top left, in from the edges (the
 * help box sat right of the radar). The radar is drawn inside an
 * 8-pixel border of its button. */
static void **touchscreen;
static float *(*Touchscreen__GetRadarButton)(void *ts);
static float *(*Touchscreen__GetSummaryButton)(void *ts);
static float *(*Touchscreen__GetTutButton)(void *ts);

static void place_hud(void)
{
	if (!*touchscreen)
		return;
	float w = OS_ScreenGetWidth(), h = OS_ScreenGetHeight();
	float *r = Touchscreen__GetRadarButton(*touchscreen);
	if (r) {
		float radius = 44.0f, cx = 76.0f, cy = h - 84.0f;
		r[3] = r[4] = 2.0f * radius + 16.0f;
		r[1] = cx - r[3] / 2.0f;
		r[2] = cy - r[4] / 2.0f;
	}
	float *s = Touchscreen__GetSummaryButton(*touchscreen);
	if (s) {
		/* the block draws 4 pixels in from its button's top and right */
		s[1] = w - 30.0f - s[3];
		s[2] = 36.0f;
	}
	float *t = Touchscreen__GetTutButton(*touchscreen);
	if (t) {
		t[1] = 32.0f;
		t[2] = 36.0f;
	}
}

static int gta3_ProcessEvents(void)
{
	if (touchscreen)
		place_hud();
	if (mobile_adjust_cam)
		stick_camera_vehicle();
	return ProcessEvents();
}

/* Help texts name buttons with ~k~~ACTION~, filled from the PC joystick
 * binding of the action (ControlsManager: the mobile build feeds the pad
 * through the PC joystick layer, Handle360Controller turning pad buttons
 * into joystick buttons). Its text, FEC_JBO ("Joy %d"), is missing from
 * the mobile .gxt, so the prompts came out empty: name the joystick button
 * by the GA36-MB's (Xbox) label instead. Joystick button n is bit n-1 of
 * Handle360Controller's mask. */
static const char *const joy_names[] = {
	[1] = "A", [2] = "RT", [3] = "LT", [4] = "B", [5] = "LB", [6] = "RB",
	[7] = "X", [8] = "Y", [9] = "BACK", [12] = "START", [13] = "UP",
	[14] = "RIGHT", [15] = "DOWN", [16] = "LEFT", [20] = "L3", [21] = "R3",
	[22] = "UP",
};

/* appends the action's button to text (the game's version lists every
 * binding: keyboard, mouse and joystick) */
static void GetWideStringOfCommandKeys(char *mgr, uint16_t action, uint16_t *text, uint16_t size)
{
	int key = *(int *)(mgr + 3340 + action * 32);
	if (key <= 0 || key >= (int)(sizeof(joy_names) / sizeof(*joy_names)) || !joy_names[key])
		return;
	int i = 0;
	while (i < size && text[i])
		i++;
	for (const char *c = joy_names[key]; *c && i + 1 < size; c++)
		text[i++] = (uint8_t)*c;
	if (i < size)
		text[i] = 0;
}

/* GTA III's mobile texts name some buttons with touch tokens (Tap
 * ::SUBMISSIONS:: to start the vigilante missions), drawn as touch-button
 * icons (Touchscreen::GetSpriteForString). The tokens become the names of
 * the pad buttons bound to those actions by default
 * (InitDefaultControlConfigJoyPad) in the loaded text: CText's CData
 * {chars, count} at +8 (GTA III has no mission text); a token is longer
 * than its name, so a string only gets shorter. CText::Load is reached
 * through its four calls (its first instructions load pc-relative). The
 * stats icons (MKILLS, MMONEY, ...) stay. */
static const struct { const char *token, *name; } touch_tokens[] = {
	{ "FIRE", "B" }, { "PUNCH", "B" }, { "THROW", "B" }, { "SPRINT", "A" },
	{ "ACCEL", "A" }, { "BRAKE", "X" }, { "DOOR", "Y" }, { "HANDBRAKE", "RB" },
	{ "LOCKON", "RB" }, { "ZOOMIN", "LT" }, { "ZOOMOUT", "RT" },
	{ "SUBMISSIONS", "L3" }, { "HORN", "R3" }, { "CAMERA", "BACK" },
};

static int replace_touch_tokens(uint16_t *c, int n)
{
	int replaced = 0;
	for (int i = 0; c && i + 4 < n; i++) {
		if (c[i] != ':' || c[i + 1] != ':')
			continue;
		int e = i + 2;
		while (e + 1 < n && c[e] && c[e] != ':')
			e++;
		if (e + 1 >= n || c[e] != ':' || c[e + 1] != ':')
			continue;
		int len = e - (i + 2);
		for (size_t t = 0; t < sizeof(touch_tokens) / sizeof(*touch_tokens); t++) {
			const char *tok = touch_tokens[t].token;
			if ((int)strlen(tok) != len)
				continue;
			int k = 0;
			while (k < len && c[i + 2 + k] == (uint8_t)tok[k])
				k++;
			if (k < len)
				continue;
			/* name over the token, the rest of the string moved up */
			int end = e + 2, z = end;
			while (z < n && c[z])
				z++;
			const char *name = touch_tokens[t].name;
			int nl = strlen(name);
			for (int j = 0; j < nl; j++)
				c[i + j] = (uint8_t)name[j];
			memmove(c + i + nl, c + end, (z - end) * 2);
			for (int j = i + nl + (z - end); j < z; j++)
				c[j] = 0;
			replaced++;
			break;
		}
	}
	return replaced;
}

static void (*CText__Load)(char *text);

static void CText__Load_hook(char *text)
{
	CText__Load(text);
	log_printf("game: %d touch tokens named in the text\n",
		replace_touch_tokens(*(uint16_t **)(text + 8), *(int *)(text + 12)));
}

/* NvFEOF and OS_FileRead read the end-of-file flag straight out of the FILE
 * as bionic lays it out (flags at +12, __SEOF 0x20); the FILE is glibc's
 * here, so a short last read never counted as the end of the file but as
 * an error, and CFileMgr::ReadLine dropped the file's last partial
 * 255-byte chunk: the last lines of the map .ide files went missing (in
 * Vice City a mission object the script needs, a crash on New Game). On
 * the phone the files come out of the OBB zip, another path. */
static int NvFEOF(const uintptr_t *f)
{
	return f[0] == 1 ? feof((FILE *)f[1]) != 0 : 1;
}

/* A save is written to GTA3sf<n>.b.tmp and then renamed through Java
 * (FileRename): done here, relative to game/ (the working directory) */
static int AND_FileRename(const char *from, const char *to, int overwrite)
{
	if (!overwrite && access(to, F_OK) == 0)
		return 0;
	if (rename(from, to) != 0) {
		log_printf("game: rename %s -> %s failed\n", from, to);
		return 0;
	}
	return 1;
}

/* videos (the logos and the opening titles, MainGameTick's first states):
 * on Android a Java MediaPlayer shows them over the game, which meanwhile
 * ticks without drawing or swapping and polls OS_MovieIsPlaying. movie.c
 * plays game/movies/; each poll, on the thread that holds the screen, shows
 * the newest picture (movie_present: the player's own GL context). A or Start
 * skips. OS_MovieStop is a 4-byte branch to AND_StopMovie, too short for a
 * hook, so AND_StopMovie is hooked. */
static SOFTFP int OS_MoviePlay(const char *file, int a2, int a3, float a4)
{
	movie_play(file);
	return 0;
}

static int AND_IsMoviePlaying(void)
{
	movie_present();
	return movie_playing();
}
static void AND_StopMovie(void) { movie_stop(); }

void patch_game(void)
{
	hook_sym("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority", OS_ThreadLaunch);
	hook_sym("_Z17OS_ScreenGetWidthv", OS_ScreenGetWidth);
	hook_sym("_Z18OS_ScreenGetHeightv", OS_ScreenGetHeight);
	hook_sym("_Z20AND_SystemInitializev", ret0);
	hook_sym("_Z13ProcessEventsb", gta3_ProcessEvents);

	/* no APK: the files are loose in game/, and NvFOpen only falls back to
	 * the APK (a Java helper) when fopen fails */
	hook_sym("_Z9NvAPKInitv", ret0);
	hook_sym("_Z9NvAPKOpenPKc", ret0);

	hook_sym("_Z14AND_FileRenamePKcS0_i", AND_FileRename);

	{
		mobile_adjust_cam = (float *)so_find_addr("Mobile_AdjustCam");
		CPad__LookAroundLeftRight = (void *)so_find_addr("_ZN4CPad19LookAroundLeftRightEv");
		CPad__LookAroundUpDown = (void *)so_find_addr("_ZN4CPad16LookAroundUpDownEv");
		static const uint8_t bl_lr[4] = { 0x21, 0xf0, 0x1e, 0xfa }, bl_ud[4] = { 0x21, 0xf0, 0x12, 0xfa };
		CPad__GetPad = (void *)so_find_addr("_ZN4CPad6GetPadEi");
		static const uint8_t cbz_touch[2] = { 0x3b, 0xb1 }, nop[2] = { 0x00, 0xbf };
		if (!patch_bl(0x15EC8C, bl_lr, (uintptr_t)stick_camera) ||
		    !patch_bl(0x15EDCC, bl_ud, (uintptr_t)return_0) ||
		    memcmp((void *)(so_base + 0x15A022), cbz_touch, 2) != 0) {
			log_printf("game: stick camera not applied\n");
			mobile_adjust_cam = NULL;
		} else {
			so_patch(0x15A022, nop, 2); /* CCam::Process: cbz -> nop */
		}
	}

	FindPlayerPed = (void *)so_find_addr("_Z13FindPlayerPedv");
	FindPlayerVehicle = (void *)so_find_addr("_Z17FindPlayerVehiclev");
	timer_user_pause = (uint8_t *)so_find_addr("_ZN6CTimer11m_UserPauseE");
	timer_code_pause = (uint8_t *)so_find_addr("_ZN6CTimer11m_CodePauseE");

	{
		the_camera = (char *)so_find_addr("TheCamera");
		CPlayerPed__DoesTargetHaveToBeBroken = (void *)so_find_addr("_ZN10CPlayerPed24DoesTargetHaveToBeBrokenE7CVectorP7CWeapon");
		CGeneral__GetATanOfXY = (void *)so_find_addr("_ZN8CGeneral11GetATanOfXYEff");
		CSprite__CalcScreenCoors = (void *)so_find_addr("_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_b");
		CWorld__GetIsLineOfSightClear = (void *)so_find_addr("_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb");
		hook_sym("_ZN10CPlayerPed14EvaluateTargetEP7CEntityPS1_Pfffb", evaluate_target);
		hook_sym("_ZN10CPlayerPed26EvaluateNeighbouringTargetEP7CEntityPS1_Pfffb", evaluate_neighbouring_target);
		static const struct { uintptr_t off; uint8_t orig[4]; } sees[] = {
			{ 0x1D1150, { 0xde, 0xf7, 0xde, 0xfd } }, { 0x1D1422, { 0xde, 0xf7, 0x75, 0xfc } },
		};
		for (size_t i = 0; i < sizeof(sees) / sizeof(*sees); i++)
			if (!patch_bl(sees[i].off, sees[i].orig, (uintptr_t)return_1))
				log_printf("game: lock-on pre-check at %#x not patched\n", (unsigned)sees[i].off);
	}

	{
		CPlayerPed__ProcessPlayerWeapon = (void *)so_find_addr("_ZN10CPlayerPed19ProcessPlayerWeaponEP4CPad");
		CPlayerPed__FindWeaponLockOnTarget = (void *)so_find_addr("_ZN10CPlayerPed22FindWeaponLockOnTargetEv");
		CPlayerPed__ClearWeaponTarget = (void *)so_find_addr("_ZN10CPlayerPed17ClearWeaponTargetEv");
		CPlayerPed__FindNextWeaponLockOnTarget = (void *)so_find_addr("_ZN10CPlayerPed26FindNextWeaponLockOnTargetEP7CEntityb");
		CPed__SetPointGunAt = (void *)so_find_addr("_ZN4CPed13SetPointGunAtEP7CEntity");
		CWeaponInfo__GetWeaponInfo = (void *)so_find_addr("_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType");
		static const uint8_t bl_weapon[4] = { 0xff, 0xf7, 0x9f, 0xf9 };
		if (!patch_bl(0x1D21CE, bl_weapon, (uintptr_t)process_player_weapon))
			log_printf("game: RB lock-on not applied\n");
	}

	CText__Load = (void *)so_find_addr("_ZN5CText4LoadEv");
	{
		static const struct { uintptr_t off; uint8_t orig[4]; } calls[] = {
			{ 0xE7554, { 0x26, 0xf1, 0x48, 0xfd } }, { 0x16A85E, { 0xa3, 0xf0, 0xc3, 0xfb } },
			{ 0x16E29E, { 0x9f, 0xf0, 0xa3, 0xfe } }, { 0x17011A, { 0x9d, 0xf0, 0x65, 0xff } },
		};
		for (size_t i = 0; i < sizeof(calls) / sizeof(*calls); i++)
			if (!patch_bl(calls[i].off, calls[i].orig, (uintptr_t)CText__Load_hook))
				log_printf("game: text call at %#x not patched\n", (unsigned)calls[i].off);
	}

	hook_sym("_Z6NvFEOFPv", NvFEOF);
	/* OS_FileRead's own copy of the check, on a raw FILE: a short read is
	 * the end of the file (ldrh r3, [r3, #12] -> b to its return 2) */
	static const uint8_t ldrh_flags[2] = { 0x9b, 0x89 }, b_eof[2] = { 0xf8, 0xe7 };
	if (memcmp((void *)(so_base + 0xDB5AE), ldrh_flags, 2) == 0)
		so_patch(0xDB5AE, b_eof, 2);
	else
		log_printf("game: OS_FileRead end-of-file patch not applied\n");

	{
		touchscreen = (void **)so_find_addr("GTouchscreen");
		Touchscreen__GetRadarButton = (void *)so_find_addr("_ZN11Touchscreen14GetRadarButtonEv");
		Touchscreen__GetSummaryButton = (void *)so_find_addr("_ZN11Touchscreen16GetSummaryButtonEv");
		Touchscreen__GetTutButton = (void *)so_find_addr("_ZN11Touchscreen12GetTutButtonEv");
	}

	/* no app-rating popup, no vibration feedback (IsCloudAvailable and
	 * NewCloudSaveAvailable already return 0 in this build; at 4 bytes
	 * they are too short to hook: the hooks overwrote each other and
	 * saving, which asks right after writing, jumped into garbage) */
	hook_sym("_Z15CheckForAppRatev", ret0);
	*(uint8_t *)so_find_addr("UseTouchSense") = 0;

	hook_sym("_Z12OS_MoviePlayPKcbbf", OS_MoviePlay);
	hook_sym("_Z18AND_IsMoviePlayingv", AND_IsMoviePlaying);
	hook_sym("_Z13AND_StopMoviev", AND_StopMovie);

	/* straight to the menu: the legal screens count as shown */
	*(uint8_t *)so_find_addr("shownLegalScreen") = 1;

	hook_sym("_ZN24CControllerConfigManager26GetWideStringOfCommandKeysEtPtt", GetWideStringOfCommandKeys);

	hook_sym("_Z25WarGamepad_GetGamepadTypev", WarGamepad_GetGamepadType);
	hook_sym("_Z28WarGamepad_GetGamepadButtonsv", WarGamepad_GetGamepadButtons);
	hook_sym("_Z25WarGamepad_GetGamepadAxisi", WarGamepad_GetGamepadAxis);
}
