<?php
/*
Plugin Name: Diffbot Widget
Plugin URI: 
Description: Displays recent articles and pictures from websites you select.
Version: 1.0
Author: Matt Wells
Author URI: 
License: GPL2


Copyright 2014 Diffbot Technologies, Inc.  (email : support@diffbot.com)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2, as 
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


add_action( 'widgets_init', create_function( '', 'register_widget( "Diffbot_Widget" );' ) );

class Diffbot_Widget extends WP_Widget {
	
	//processes the widget
	function diffbot_widget() {
		$widget_ops = array(
			'classname' => 'diffbot_class',
			'description' => 'Displays recent articles and pictures from websites you select. No RSS required.' );
		$this->WP_Widget( 'Diffbot_Widget', 'Diffbot Widget',
			$widget_ops );
	}
	
	//displays widget form in the admin dashboard
	function form($instance) {
		$defaults = array('title' => 'Diffbot Widget',
				  'sitelist' => '',
				  'show_dates' => 'no',
				  'show_images' => 'yes',
				  'show_searchbox' => 'yes',
				  'widget_style_tag' => '<style>div.diffbot_widget {font-size:12px;font-family:arial;background-color:transparent;color:black;}span.diffbot_title { font-size:16px;font-weight:bold;}span.diffbot_summary { font-size:12px;} span.diffbot_date { font-size:12px;}span.diffbot_prevnext { font-size:12px;font-weight:bold;}</style>',
				  'query' => 'type:article gbsortbyint:gbspiderdate',
				  'widget_height' => '300' );
		$instance = wp_parse_args( (array)$instance, $defaults );
		
		?>

		<div>
		<!-- style="background-image:url('http://www.diffbot.com/img/diffy-b.png');background-repeat:no-repeat;background-size:80px 80px;">-->


		<p>Title: <input class="widefat" name="<?php echo $this->get_field_name( 'title' );?>" type="text"
			value="<?php echo esc_attr( $instance['title'] ); ?>" /></p>
		<p>
		<?php
			// sitelist
			echo 'Websites to Include: <sup>[?]</sup><br>';
			echo '<textarea style=width:100%; name="';
			echo $this->get_field_name( 'sitelist' );
			echo '">'.esc_attr($instance['sitelist']);
			echo '</textarea><br>';
			echo '<br>';
			
			// query box
			echo 'Query: <sup>[?]</sup><br>';
			echo '<textarea style=width:100%; name="';
			echo $this->get_field_name( 'query' );
			echo '">'.esc_attr($instance['query']);
			echo '</textarea><br>';
			echo '<br>';


			// widget height
			echo 'Widget Height: ';
			echo '<input type=text size=5 name="';
			echo $this->get_field_name( 'widget_height' );
			echo '" value="';
			echo esc_attr($instance['widget_height']);
			echo '"> pixels';
			echo '<br>';
			echo '<br>';

			// show dates
			echo 'Show Dates: ';
			echo '<input type=checkbox name="';
			echo $this->get_field_name( 'show_dates' );
			echo '" value="yes"';
			if ( $instance['show_dates'] == 'yes' )
			   echo ' checked';
			echo '>';
			echo '<br>';
			echo '<br>';

			// show images
			echo 'Show Images:';
			echo '<input type=checkbox name="';
			echo $this->get_field_name( 'show_images' );
			echo '" value="yes"';
			if ( $instance['show_images'] == 'yes' )
			   echo ' checked';
			echo '>';
			echo '<br>';
			echo '<br>';

			// custom widget style tag
			echo 'Widget style tag: <sup>[?]</sup><br>';
			echo '<textarea style=width:100%; name="';
			echo $this->get_field_name( 'widget_style_tag' );
			echo '">'.esc_attr($instance['widget_style_tag']);
			echo '</textarea><br>';

			// submit the sites for spidering now
			//$url  = 'http://neo.diffbot.com:8100/admin/settings';
			$url  = 'http://127.0.0.1:8000/admin/settings';
			$url .= '?format=widget';
			$url .= '&c=widget';
			$url .= '&appendtositelist=';
			$url .= urlencode($instance['sitelist']);
			// fetch url like it is a fake image. this will
			// allow the spiders to start crawling the sites.
			echo '<img height=0 width=0 src="'.$url.'">';
		?>

		</div>
		<?php
	}//end of function form
	
	function widget($args, $instance) {
		extract($args);
		
		echo $before_widget;
		
		
		$title = $instance['title'];
		
		if(!isset($title))
			$title = 'Diffbot Widget';

		echo '<b>'.$before_title . $title . $after_title.'</b></br>';
		

		echo '<script type="text/javascript">function diffbot_handler() {if(this.readyState != 4 ) return;document.getElementById("diffbot_widget").innerHTML=this.responseText;}</script>';

		// display the style tag	
		$style = $instance['widget_style_tag'];
		echo $style;
		
		// use ajax to send query to neo.diffbot.com and put
		// the results here in this div
		//$url  = 'http://neo.diffbot.com:8100/search';
		$url  = 'http://127.0.0.1:8000/search';
		$url .= '?format=widget';
		// use a special index for widget crawling
		$url .= '&c=widget';
		$url .= '&q=';
		$url .= urlencode($instance['query']);
		$url .= '&sites=';
		$url .= urlencode($instance['sitelist']);
		
		// then the containing div. set the "id" so that the
		// style tag the user sets can control its appearance.
		// when the browser loads this the ajax sets the contents
		// to the reply from neo.
		//echo '<div id=diffbot_widget onclick="var client=new XMLHttpRequest();client.onreadystatechange=diffbot_handler;client.open(\'GET\',\''.$url.'\');client.send();">';

		echo '<div id=diffbot_widget>';

		// get the search results from neo as soon as this div is
		// being rendered, and set its contents to them
		echo '<script type=text/javascript>var client=new XMLHttpRequest();client.onreadystatechange=diffbot_handler;client.open(\'GET\',\''.$url.'\');client.send();</script>';


		echo 'Waiting for Diffbot...';


		// end the containing div
		echo '</div>';

		echo 'Powered by <a href=http://www.diffbot.com/>Diffbot</a> and <a href=http://www.gigablast.com/>Gigablast</a>';
			
		echo $after_widget;
	}//end of function widget
	
}?>
