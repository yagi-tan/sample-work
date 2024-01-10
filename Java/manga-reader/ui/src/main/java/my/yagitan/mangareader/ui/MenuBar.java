/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
package my.yagitan.mangareader.ui;

import java.awt.event.ActionEvent;
import java.util.concurrent.ExecutionException;
import javax.swing.ButtonGroup;
import javax.swing.JFileChooser;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JRadioButtonMenuItem;
import javax.swing.SwingWorker;
import javax.swing.filechooser.FileNameExtensionFilter;

/** Main window menu bar. */
class MenuBar {
	/** Creates populated menu bar.
	 * @return JMenuBar object.
	 */
	public static JMenuBar create(MainWindow mainWnd, EvtHandlers evtHandlers) {
		final JMenu menuFile = new JMenu("File");
		menuFile.add(new JMenuItem("Open")).addActionListener((ActionEvent e) -> {
			final JFileChooser fChooser = new JFileChooser();
			fChooser.setFileFilter(new FileNameExtensionFilter("Image files", "jpg", "jpeg", "gif", "png"));
			fChooser.setMultiSelectionEnabled(false);
			
			if (fChooser.showOpenDialog(mainWnd.viewArea) == JFileChooser.APPROVE_OPTION) {
				new SwingWorker<Boolean, Void>() {
					@Override public Boolean doInBackground() {
						return evtHandlers.setViewImgList(fChooser.getSelectedFile(), 0);
					}
					@Override public void done() {
						try {
							if (get()) {
								mainWnd.viewArea.repaint();
							}
							else {
								JOptionPane.showMessageDialog(mainWnd.viewArea,
									"Error opening file '" + fChooser.getSelectedFile() + "'.", "Error",
									JOptionPane.ERROR_MESSAGE);
							}
						}
						catch (final InterruptedException | ExecutionException e) {
							System.out.println("Error opening file: " + e);
						}
					}
				}.execute();
			}
		});
		menuFile.addSeparator();
		menuFile.add(new JMenuItem("Exit")).addActionListener((ActionEvent e) -> {
			evtHandlers.exit();
		});
		
		final JMenu menuView = new JMenu("View");
		{
			final ButtonGroup btnGroup = new ButtonGroup();
			final JMenu menuViewLayout = new JMenu("Layout");
			
			for (var layout : Enums.ImageLayout.values()) {
				final var rb = new JRadioButtonMenuItem(layout.toString());
				
				rb.addActionListener((ActionEvent e) -> {
					evtHandlers.setViewImgLayout(layout);
					mainWnd.viewArea.repaint();
				});
				if (evtHandlers.getViewImgLayout() == layout) {
					rb.setSelected(true);
				}
				
				btnGroup.add(rb);
				menuViewLayout.add(rb);
			}
			
			menuView.add(menuViewLayout);
		}
		{
			final ButtonGroup btnGroup = new ButtonGroup();
			final JMenu menuViewSizing = new JMenu("Sizing");
			
			for (var sizing : Enums.ImageSizing.values()) {
				final var rb = new JRadioButtonMenuItem(sizing.toString());
				
				rb.addActionListener((ActionEvent e) -> {
					evtHandlers.setViewImgSizing(sizing);
					mainWnd.viewArea.repaint();
				});
				if (evtHandlers.getViewImgSizing() == sizing) {
					rb.setSelected(true);
				}
				
				btnGroup.add(rb);
				menuViewSizing.add(rb);
			}
			
			menuView.add(menuViewSizing);
		}
		{
			final JMenu menuViewRotate = new JMenu("Rotate");
			
			menuViewRotate.add(new JMenuItem("Rotate 90 degrees CW")).addActionListener((ActionEvent e) -> {
				if (evtHandlers.rotateViewImg(1, false)) {
					mainWnd.viewArea.repaint();
				}
			});
			menuViewRotate.add(new JMenuItem("Rotate 90 degrees CCW")).addActionListener((ActionEvent e) -> {
				if (evtHandlers.rotateViewImg(1, true)) {
					mainWnd.viewArea.repaint();
				}
			});
			menuViewRotate.add(new JMenuItem("Rotate 180 degrees")).addActionListener((ActionEvent e) -> {
				if (evtHandlers.rotateViewImg(2, true)) {
					mainWnd.viewArea.repaint();
				}
			});
			
			menuView.add(menuViewRotate);
		}
		
		final JMenuBar menuBar = new JMenuBar();
		menuBar.add(menuFile);
		menuBar.add(menuView);
		
		return menuBar;
	}
}
