/*
 * pc-mixer CARD: the playback levels of a PC's sound card (pc-audio.sh, at boot).
 *
 * The kernel leaves an HD Audio codec's outputs at their lowest step, which on most codecs is -127 dB
 * (silence), and switched off; on Linux a sound server or alsactl raises them. Android's audio HAL sets
 * only the master level, so every output switch goes on and every output level to its top, 0 dB on HD
 * Audio converters. Capture is left to the HAL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <tinyalsa/asoundlib.h>

static const char *const OUTPUTS[] = { "Master", "Speaker", "Headphone", "PCM", "Front", "Line Out", "Bass Speaker" };

static void set_all(struct mixer *mixer, const char *name, int top)
{
	struct mixer_ctl *ctl = mixer_get_ctl_by_name(mixer, name);
	if (!ctl)
		return;
	int value = top ? mixer_ctl_get_range_max(ctl) : 1;
	for (unsigned i = 0; i < mixer_ctl_get_num_values(ctl); i++)
		mixer_ctl_set_value(ctl, i, value);
	printf("%s = %d\n", name, value);
}

int main(int argc, char **argv)
{
	char name[64];
	struct mixer *mixer = mixer_open(argc > 1 ? (unsigned)atoi(argv[1]) : 0);
	if (!mixer)
		return fprintf(stderr, "pc-mixer: no mixer on card %s\n", argc > 1 ? argv[1] : "0"), 1;
	for (size_t i = 0; i < sizeof OUTPUTS / sizeof *OUTPUTS; i++) {
		snprintf(name, sizeof name, "%s Playback Switch", OUTPUTS[i]);
		set_all(mixer, name, 0);
		snprintf(name, sizeof name, "%s Playback Volume", OUTPUTS[i]);
		set_all(mixer, name, 1);
	}
	mixer_close(mixer);
	return 0;
}
