POV WAND - HOW TO CHANGE WHAT IT SHOWS
======================================
Plug the wand into your computer. A drive called WAND appears.

1. CHANGE THE WORDS
   Open message.txt in Notepad, type your words, save.
     - One line  -> big letters.
     - Two lines -> two rows of smaller letters.
     - Letters A-Z, numbers, and  ! ? . , : ' ( ) + - / = * _  work.
       A  ~  draws a little heart.
   The wand picks the new words up by itself. Keep it still for a second
   after saving, then wave it left-and-right.
   Wave briskly. A slow wave does not turn into a steady picture.

2. SHOW A PICTURE INSTEAD
   Open image.bmp in Paint. Draw with BLACK on the white background.
   (blank_template.bmp is an empty canvas, 64 x 16 pixels. In Paint, use
   View > Zoom in a lot, or draw with the pencil tool.)
   Save as "Monochrome Bitmap" (or 24-bit Bitmap) with the SAME name, image.bmp.
   Keep the picture 16 pixels tall; it can be as wide as you like.
   Then open settings.txt and set   SHOW = image   (or   SHOW = both ).

3. IT GOES TO SLEEP
   Put the wand down and the lights go out after a few seconds.
   Pick it up, or give it a small shake, and it wakes.
   On its battery the chip itself powers down. A shake on the sensor brings
   it back.
   While the wand is plugged into a computer the drive stays put, so a file
   you are editing is not snatched away. The lights still go out.
   To leave it always on, open settings.txt and set   SLEEP = no.

4. IF SOMETHING IS WRONG
   - The bottom LED blinks once every 2 seconds when the wand is idle (that means "on").
   - If it blinks TWICE, one of your files has a problem. Open settings.txt / message.txt /
     image.bmp and check them, or ask whoever set the wand up.
   - Words backwards?  Change SIGN = 1 to SIGN = -1 in settings.txt.
   - Upside down?      Set FLIP_VERTICAL = yes.
