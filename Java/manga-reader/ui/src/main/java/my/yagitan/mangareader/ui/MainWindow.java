/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.ui;

import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.Point;
import java.awt.event.ComponentAdapter;
import java.awt.event.ComponentEvent;
import java.awt.event.MouseEvent;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;
import javax.swing.SwingWorker;
import javax.swing.event.MouseInputAdapter;

/** Main window for the application. */
public class MainWindow {
	/** Constructor. */
	public MainWindow() {}
	
	/** Starts main window.
	 * @param evtHandlers Handlers to all events fired in UI.
	 * @param width Initial window width (in pixels).
	 * @param height Initial window height (in pixels).
	 * @return Always true.
	 */
	public boolean start(EvtHandlers evtHandlers, int width, int height) {
		SwingUtilities.invokeLater(() -> {
			//dummy object implementing MouseInputAdapter class
			final MouseInputAdapter mouseInput = new MouseInputAdapter() {
				@Override public void mousePressed(MouseEvent e) {
					mouseClickBtn = e.getButton();
					if (mouseClickBtn == MouseEvent.BUTTON1) {
						mouseDragPt = e.getPoint();
					}
				}
				@Override public void mouseDragged(MouseEvent e) {
					if (mouseClickBtn == MouseEvent.BUTTON1) {		//drag event doesn't have button
						final Point curPt = e.getPoint();
						
						if (evtHandlers.moveViewImg(curPt.x - mouseDragPt.x, curPt.y - mouseDragPt.y)) {
							viewArea.repaint();
						}
						mouseDragPt = curPt;
					}
				}
			};
			
			viewArea = new JPanel() {
				@Override protected void paintComponent(Graphics g) {
					super.paintComponent(g);
					
					final var g2 = (Graphics2D) g;
					final var imgs = evtHandlers.getViewImgList();
					final var imgXforms = evtHandlers.getViewImgXformList();
					
					if ((imgs != null) && (imgXforms != null) && (imgs.size() == imgXforms.size())) {
						for (int i = 0; i < imgs.size(); ++i) {
							g2.drawImage(imgs.get(i), imgXforms.get(i), this);
						}
					}
				}
			};
			viewArea.addComponentListener(new ComponentAdapter() {
				@Override public void componentResized(ComponentEvent e) {
					new SwingWorker<Void, Void>() {
						@Override public Void doInBackground() {
							evtHandlers.setViewAreaDimension(viewArea.getHeight(), viewArea.getWidth());
							return null;
						}
						@Override public void done() {
							viewArea.repaint();
						}
					}.execute();
				}
			});
			viewArea.addMouseListener(mouseInput);
			viewArea.addMouseMotionListener(mouseInput);
			viewArea.addMouseWheelListener(mouseInput);
			viewArea.setBackground(Color.BLACK);
			viewArea.setPreferredSize(new Dimension(width, height));
			
			final JFrame mainFrame = new JFrame("Manga Reader");
			mainFrame.setJMenuBar(MenuBar.create(this, evtHandlers));
			mainFrame.setContentPane(viewArea);
			mainFrame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
			mainFrame.pack();
			mainFrame.setVisible(true);
		});
		
		return true;
	}
	
	/** Image view area. */
	JPanel viewArea;
	
	/** Mouse drag event starting point. */
	Point mouseDragPt;
	
	/** Mouse click event last button. */
	int mouseClickBtn;
}
