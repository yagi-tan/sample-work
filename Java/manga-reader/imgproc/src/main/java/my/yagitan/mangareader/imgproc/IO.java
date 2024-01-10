/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.imgproc;

import java.awt.Image;
import java.io.File;
import java.io.IOException;
import javax.imageio.ImageIO;
import javax.swing.ImageIcon;

/** Deals with image input/output processes. */
public class IO {
	private IO() {}
	
	/** Reads image data from file.
	 * @param file Target file.
	 * @return Image data object or null if error occurred.
	 */
	public static Image readFile(File file) {
		Image img = null;
		
		if ((file != null) && file.isFile()) {
			if (file.getName().endsWith("gif")) {					//simple ImageIO.read() doesn't work
				final ImageIcon imgGif = new ImageIcon(file.getPath());
				
				//load status value kinda unreliable, have to check dimension for error
				if ((imgGif.getIconHeight() >= 0) && (imgGif.getIconWidth() >= 0)) {
					img = imgGif.getImage();
				}
				else {
					System.err.println(String.format("Error reading GIF file '%s': status:%d h:%d w:%d",
						file.getPath(), imgGif.getImageLoadStatus(), imgGif.getIconHeight(),
						imgGif.getIconWidth()));
				}
			}
			else {
				try {
					img = ImageIO.read(file);
				}
				catch (final IOException e) {
					System.err.println("Error reading file '" + file + "': " + e);
				}
			}
		}
		
		return img;
	}
}
