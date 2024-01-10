/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.ui;

import java.awt.Image;
import java.awt.geom.AffineTransform;
import java.io.File;
import java.util.List;

/** Handlers to all events fired in user interface.
 * @author yagi-tan
 */
public interface EvtHandlers {
	/** UI exits. */
	void exit();
	
	/** Getter for image display layout.
	 * @return Current image layout constant.
	 */
	Enums.ImageLayout getViewImgLayout();
	
	/** Getter for image list for display.
	 * @return Current image list.
	 */
	List<Image> getViewImgList();
	
	/** Getter for image display sizing.
	 * @return Current image sizing constant.
	 */
	Enums.ImageSizing getViewImgSizing();
	
	/** Getter for image transformation list.
	 * @return Current image transformation list.
	 */
	List<AffineTransform> getViewImgXformList();
	
	/** Move image in view area.
	 * @param x Movement in x-axis, in pixels. Positive value -&gt; right.
	 * @param y Movement in y-axis, in pixels. Positive value -&gt; down.
	 * @return True if view area needs to be repainted.
	 */
	boolean moveViewImg(int x, int y);
	
	/** Rotate image in view area.
	 * @param quadrantQt Image rotation count in quadrant. Always positive.
	 * @param cw Image rotation direction. True if it's in clockwise (CW).
	 * @return True if view area needs to be repainted.
	 */
	boolean rotateViewImg(int quadrantQt, boolean cw);
	
	/** Setter for view area dimension (e.g. when changing after resized).
	 * @param height New view area height.
	 * @param width New view area width.
	 */
	void setViewAreaDimension(int height, int width);
	
	/** Setter for image display layout.
	 * @param imgLayout Image layout constant.
	 */
	void setViewImgLayout(Enums.ImageLayout imgLayout);
	
	/** Setter for image list for display.
	 * @param file Target image file.
	 * @param idx View list index. Must be between 0 and 1.
	 * @return True if image file is successfully read and registered into view list.
	 */
	boolean setViewImgList(File file, int idx);
	
	/** Setter for image display sizing.
	 * @param imgSizing Image sizing constant.
	 */
	void setViewImgSizing(Enums.ImageSizing imgSizing);
}
