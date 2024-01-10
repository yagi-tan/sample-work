/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.ui;

/** Enumeration constants. */
public class Enums {
	private Enums() {}
	
	public enum ImageLayout {
		CONTINUOUS("Continuous"),
		DOUBLE_NORMAL("Double pages (normal mode)"),
		DOUBLE_MANGA("Double pages (manga mode"),
		SINGLE("Single page");
		
		ImageLayout(String desc) {
			this.desc = desc;
		}
		
		@Override public String toString() {
			return desc;
		}
		
		private final String desc;
	}
	public enum ImageSizing {
		ACTUAL_SIZE("Actual size"),
		FIT_HEIGHT("Fit height"),
		FIT_HEIGHT_WIDTH("Fit height and width"),
		FIT_WIDTH("Fit width"),
		MANUAL("Manual");
		
		ImageSizing(String desc) {
			this.desc = desc;
		}
		
		@Override public String toString() {
			return desc;
		}
		
		private final String desc;
	}
}
