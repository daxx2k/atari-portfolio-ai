# Validation

## Release checks

Fresh release-folder compilation, client binary reproducibility, native DSP tests, client emulation, compact radio packet/config persistence tests and a credential audit are recorded in `validation/release-checks.json` after completion.

## Previous actual-hardware feedback

The Operator confirmed typed text plus speech, faster continuous speech, radio playback, responsive radio controls, microphone conversation, camera preview, battery operation after polarity correction and acceptable voice/radio balance. Firmware 0.4.15 was flashed with hash verification. A recorded spoken reply streamed before download completed, with HTTP 200 and zero underruns in that sample; radio resumed afterwards.

Those are observations on the development assembly. They are not a new end-to-end test on a second device, a guarantee for every stream or a measured speaker SPL result. Raw prompts, credentials, device identifiers and personal logs are omitted from the repository.
