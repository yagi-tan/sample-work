/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.imgproc;

import java.awt.geom.Point2D;
import java.awt.Image;
import java.awt.geom.AffineTransform;

/** Deals with image transformation. */
public class Transform {
	private Transform() {}
	
	/** Centres image position within view area along an axis.
	 * @param viewLength View area length along the axis.
	 * @param imgLength Image length along the axis.
	 * @return Starting location (usually top-left corner) of the image.
	 */
	public static double centreImageAxis(int viewLength, double imgLength) {
		return (viewLength > imgLength) ? (viewLength - imgLength) / 2.0 : 0.0;
	}
	
	/** Calculates effective image dimension after being transformed.
	 * @param img Image object.
	 * @param scale Size scaling.
	 * @param rot Rotation in quadrant. Positive value will rotate image in CCW.
	 * @return {@link java.awt.geom.Point2D.Double Point2D.Double} object containing image dimension after
	 *	transformation.
	 */
	public static Point2D.Double getEffImageDim(Image img, double scale, int rot) {
		final Point2D.Double dim = new Point2D.Double();
		
		if ((rot % 2) == 0) {										//rotation in multiple of 180 degrees
			dim.x = img.getWidth(null);
			dim.y = img.getHeight(null);
		}
		else {
			dim.x = img.getHeight(null);
			dim.y= img.getWidth(null);
		}
		
		dim.x *= scale;
		dim.y *= scale;
		
		return dim;
	}
	
	/** Moves image along view area. In case image is larger than axis, any image side will never goes within
	 * its respective axis end. Else, no movement is done.
	 * @param imgAxisLen Image length along specific axis.
	 * @param viewAxisLen View area length along specific axis.
	 * @param pos Current image position on the axis.
	 * @param offset Offset to move from current image position.
	 * @return New image position.
	 */
	public static double moveImageAlongAxis(double imgAxisLen, int viewAxisLen, double pos, int offset) {
		final double newPos;
		
		if (viewAxisLen < imgAxisLen) {
			if (offset > 0) {
				newPos = ((pos + offset) >= 0) ? 0 : (pos + offset);
			}
			else if (offset < 0) {
				final double limit = viewAxisLen - imgAxisLen;
				newPos = ((pos + offset) <= limit) ? limit : (pos + offset);
			}
			else {
				newPos = pos;
			}
		}
		else {
			newPos = pos;
		}
		
		return newPos;
	}
	
	/** Fits image into bounding box while keeping its aspect ratio.
	 * @param imgHeight Image height.
	 * @param imgWidth Image width.
	 * @param boxHeight Bounding box height.
	 * @param boxWidth Bounding box width.
	 * @return Image scaling factor.
	 */
	public static double scaleImageToFitBox(double imgHeight, double imgWidth, int boxHeight, int boxWidth) {
		return Math.min(boxHeight / imgHeight, boxWidth / imgWidth);
	}
	
	/** Sets image transformation object.
	 * @param xform Image transformation object.
	 * @param scale Size scaling.
	 * @param dx Offset in x-axis from view area top-left point.
	 * @param dy Offset in y-axis from view area top-left point.
	 * @param rot Rotation in quadrant. Positive value will rotate image in CCW.
	 */
	public static void setXform(AffineTransform xform, double scale, double dx, double dy, int rot) {
		xform.setToScale(scale, scale);
		//xform.quadrantRotate(rot);
		xform.translate(dx / scale, dy / scale);
		xform.rotate(rot * Math.PI / 6.0, 400, 225);
	}
}
