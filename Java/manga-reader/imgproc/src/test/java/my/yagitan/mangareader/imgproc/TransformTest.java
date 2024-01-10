/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.imgproc;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

import java.awt.geom.AffineTransform;

/**
 *
 * @author yagi-tan
 */
public class TransformTest {
	public TransformTest() {}

	/**
	 * Test of setXform method, of class Transform.
	 */
	@Test
	public void testSetXform() {
		System.out.println("setXform");
		AffineTransform xform = new AffineTransform();
		int dx = 0;
		int dy = 0;
		int rot = 0;
		double scale = 1.0;
		Transform.setXform(xform, scale, dx, dy, rot);
		//assertEquals(expResult, result);
		// TODO review the generated test code and remove the default call to fail.
		fail("Haven't implemented yet.");
	}
	
}
