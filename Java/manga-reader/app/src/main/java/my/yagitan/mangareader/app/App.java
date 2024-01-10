/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.app;

import my.yagitan.mangareader.imgproc.IO;
import my.yagitan.mangareader.imgproc.Transform;
import my.yagitan.mangareader.ui.Enums.*;
import my.yagitan.mangareader.ui.EvtHandlers;
import my.yagitan.mangareader.ui.MainWindow;

import java.awt.geom.Point2D;
import java.awt.Image;
import java.awt.geom.AffineTransform;
import java.io.File;
import java.util.ArrayList;
import java.util.List;
import my.yagitan.mangareader.ui.Enums;

/** Application centre. */
public class App implements EvtHandlers {
	/** Constructor. */
	App() {
		imgs.add(null);												//dummy images
		imgs.add(null);
		imgXforms.add(new AffineTransform());						//identity transform
		imgXforms.add(new AffineTransform());
		
		imgLayout = ImageLayout.SINGLE;
		imgSizing = ImageSizing.FIT_HEIGHT_WIDTH;
		
		imgRotateQuad = 0;
		viewAreaHeight = 450;
		viewAreaWidth = 800;
	}
	
	@Override public void exit() {
		System.out.println("Exit clicked.");
	}
	
	@Override public ImageLayout getViewImgLayout() {
		return imgLayout;
	}
	
	@Override public List<Image> getViewImgList() {
		return imgs;
	}
	
	@Override public ImageSizing getViewImgSizing() {
		return imgSizing;
	}
	
	@Override public List<AffineTransform> getViewImgXformList() {
		return imgXforms;
	}
	
	@Override public boolean moveViewImg(int x, int y) {
		boolean hasChanged = false;
		
		switch (imgLayout) {
		case SINGLE:
			final Image img = imgs.get(0);
			
			if (img != null) {
				final AffineTransform imgXform = imgXforms.get(0);
				final double scale = imgXform.getScaleX();
				final Point2D.Double effDim = Transform.getEffImageDim(img, scale, imgRotateQuad);
				final double oldDx = imgXform.getTranslateX(), oldDy = imgXform.getTranslateY();
				final double dx = Transform.moveImageAlongAxis(effDim.x, viewAreaWidth, oldDx, x),
					dy = Transform.moveImageAlongAxis(effDim.y, viewAreaHeight, oldDy, y);
				
				if ((dx != oldDx) || (dy != oldDy)) {				//there's new movement
					Transform.setXform(imgXform, scale, dx, dy, imgRotateQuad);
					hasChanged = true;
				}
			}
			break;
		default:
			break;
		}
		
		return hasChanged;
	}
	
	@Override public boolean rotateViewImg(int quadrantQt, boolean cw) {
		final int oldImgRotateQuad = imgRotateQuad;
		
		if (cw) {
			imgRotateQuad -= quadrantQt;
		}
		else {
			imgRotateQuad += quadrantQt;
		}
		imgRotateQuad %= 4;											//keep value within [-3,3] range
		
		if (imgRotateQuad != oldImgRotateQuad) {
			layoutImgs();
			return true;
		}
		
		return false;
	}
	
	@Override public void setViewAreaDimension(int height, int width) {
		viewAreaHeight = height;
		viewAreaWidth = width;
		layoutImgs();
	}
	
	@Override public void setViewImgLayout(Enums.ImageLayout imgLayout) {
		if (this.imgLayout != imgLayout) {
			this.imgLayout = imgLayout;
			layoutImgs();
		}
	}
	
	@Override public boolean setViewImgList(File file, int idx) {
		boolean result = false;
		
		if ((idx >= 0) && (idx < 2)) {
			final Image img = IO.readFile(file);
			
			if (img != null) {
				imgs.set(idx, img);
				layoutImgs();
				result = true;
			}
		}
		
		return result;
	}
	
	@Override public void setViewImgSizing(Enums.ImageSizing imgSizing) {
		if (this.imgSizing != imgSizing) {
			this.imgSizing = imgSizing;
			layoutImgs();
		}
	}
	
	/** Starts application. */
	void start() {
		new MainWindow().start(this, viewAreaWidth, viewAreaHeight);
	}
	
	/** Updates all image transformations for image positioning in view area. */
	void layoutImgs() {
		switch (imgLayout) {
		case CONTINUOUS:
			break;
		case DOUBLE_MANGA:
			break;
		case DOUBLE_NORMAL:
			break;
		case SINGLE:
			final Image img = imgs.get(0);
			final AffineTransform imgXform = imgXforms.get(0);
			
			if (img != null) {
				final double scale;
				
				//get effective image dimension after rotate operation
				Point2D.Double effDim = Transform.getEffImageDim(img, 1.0, imgRotateQuad);
				
				//calculate image scaling against view area based on sizing mode
				switch (imgSizing) {
				case ACTUAL_SIZE:
					scale = 1.0;
					break;
				case FIT_HEIGHT:
					scale = viewAreaHeight / effDim.y;
					break;
				case FIT_HEIGHT_WIDTH:
					scale = Transform.scaleImageToFitBox(effDim.y, effDim.x, viewAreaHeight,
						viewAreaWidth);
					break;
				case FIT_WIDTH:
					scale = viewAreaWidth / effDim.x;
					break;
				case MANUAL:
					scale = 1.0;
					break;
				default:
					scale = 1.0;
					break;
				}
				effDim = Transform.getEffImageDim(img, scale, imgRotateQuad);
				
				Transform.setXform(imgXform, scale,
					Transform.centreImageAxis(viewAreaWidth, effDim.x),
					Transform.centreImageAxis(viewAreaHeight, effDim.y), imgRotateQuad);
			}
			break;
		}
	}
	
	public static void main(String[] args) {
		System.out.println("Starting app");
		new App().start();
		System.out.println("Closing app");
	}
	
	/** List of images to be shown in view area. */
	private final List<Image> imgs = new ArrayList<>(2);
	/** List of image transformations done when rendered in view area. */
	private final List<AffineTransform> imgXforms = new ArrayList<>(2);
	/** Image rotation in quadrant. Positive value will rotate image in CCW. */
	private int imgRotateQuad;
	/** Current view area dimension. */
	private int viewAreaHeight, viewAreaWidth;
	/** Image positioning in view area. */
	private ImageLayout imgLayout;
	private ImageSizing imgSizing;
}
