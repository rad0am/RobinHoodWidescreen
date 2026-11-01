# Main menu at the native resolution

Now: the main menu draws a fixed ~1024×768 frame centered on the screen, the rest is black. Dialog scaling
([dialogs.md](dialogs.md)) is turned off outside missions on purpose, because giving the virtual size to all menu code broke
the main menu built right after a mission.

- [ ] Reuse the dialog scaling for the main menu: virtual screen of height 768 → the frame fills the screen height, stretched
  ×`screen height / 768`. Start the dialog mode when the main menu asks for the screen size (its own caller address,
  like `DIALOG_START_CALLERS`), and end it when a mission loads.
- [ ] Sides: the frame is 4:3, so on 21:9 there will still be bars left and right. Options: leave them black, fill with a blurred or
  mirrored copy of the background, or stretch only the background image.
- [ ] Sub-menus (options, load game, campaign map) and the mouse cursor must follow the same scale.
- [ ] Same blurry font concern as in dialogs (see [dialogs.md](dialogs.md#blurry-font-in-dialogs)).
