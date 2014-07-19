<?php

/**
 * @file shanti_kmaps_utils.api.inc
 */

/**
 * Simple class for putting together a URL for the API call, and doing an HTTP request
 * for retrieving information about KMaps.
 * All public methods return $this and can be chained together, except the execute(),
 * which returns the response data.
 * 
 * TO ADD:
 * -- Search context,i.e. which branch one is browsing. This should have
 * a site-wide default mode as well as a content-type specific override. 
 * -- JSONP on/off setting
 * -- Search scope setting (i.e. if labels or also descriptions, etc.)
 * -- Adapt to newer API
 * 
 * NEEDED FROM ANDRES:
 * -- A flat result list with parent identified (i.e. add element to list results)
 * -- A more complete item for name searches; i.e. with ancestors
 * -- A csv API
 */
 

class KMapsAPI {
  
  const KMAPS_URL_STEM = 'kmaps.virginia.edu/features';
  
  private $api_url            = ''; // set in constructor
  private $domain             = ''; // set in constructor; 'subjects','places','dev-subjects','dev-places'
  private $debug_mode         = 0; // or 1
  private $arguments          = array(); // URL arguments for string searches
  private $kmap_string        = '';   
  private $kmap_ids           = array();
  private $kmap_id            = 0;
  private $parse_mode         = 'autocomplete'; // or 'raw'
  private $fetch_mode         = 'by_name';   // or 'by_fid' or 'by_fid_cached'
  private $results_format     = 'json'; // or 'xml'
  private $results_page       = '1'; // page number of results
  private $results_per_page   = '100'; // the count of results per page
  private $results_scope      = 'names';  // Also 'full_text' 
  private $results_view_code  = 'roman.popular'; // the view code that the features' information should be returned in (e.g. "roman.popular")

  public function __construct($domain = 'subjects') {
    $this->domain     = $domain;
    $this->api_url    = 'http://' . $this->domain . '.' . KMapsAPI::KMAPS_URL_STEM;
    $this->debug_mode = variable_get('shanti_kmaps_view_debug_mode',0);
    return $this;
  }
  
  public function setString($string) {
    $this->kmap_string = $string;
    return $this;
  }
  
  public function setKMapID($kmap_id) {
    $this->kmap_id = $kmap_id;
    return $this;
  }
  
  public function pushKMapIDs($kmap_id) {
    $this->kmap_ids[] = $kmap_id;
    return $this;
  }
  
  public function getKMapIDsString($delim = ',') {
    return implode($delim, $this->kmap_ids);
  }

  public function addArgument($key, $value) {
    $this->arguments[$key] = $value;
    return $this;
  }
    
  public function setFormatToJSON() {
    $this->results_format = 'json';
    return $this;
  }

  public function setFormatToXML() {
    $this->results_format = 'xml';
    return $this;
  }
  
  public function setParseModeToAutocomplete() {
    $this->parse_mode = 'autocomplete';
    return $this;
  }

  public function setParseModeToRaw() {
    $this->parse_mode = 'raw';
    return $this;
  }
  
  public function setFetchModeToName() {
    $this->fetch_mode = 'by_name';
  }

  public function setFetchModeToID() {
    $this->fetch_mode = 'by_fid';
  }

  public function setFetchModeToIDCached() {
    $this->fetch_mode = 'by_fid_cached';
  }

  /**
   * Executes the request. Returns the response data. This is going
   * to be overridden by more specific requests (see below).
   */
  public function execute() {
    $raw_url = $this->api_url;
    
    // Build the base URL for each fetch mode
    if ($this->fetch_mode == 'by_name') {
      $raw_url .= '/by_name/' . $this->kmap_string . '.' . $this->results_format;
    } elseif ($this->fetch_mode == 'by_fid') {
      $kmap_id_string = implode(',',$this->kmap_ids);
      $raw_url .= '/by_fid/' . $kmap_id_string . '.' . $this->results_format;
    } elseif ($this->fetch_mode == 'by_fid_cached') {
      $raw_url .= '/' . $this->kmap_id . '.' . $this->results_format;
    } else {
      // Anomalous; do nothing for now. Or define a default.
    }
    
    // Let Drupal construct the full URL
    $options = array();
    $options['absolute'] = TRUE;
    $options['query'] = $this->arguments; // ONLY IF STRING SEARCH
    $request_url = url($raw_url, $options);
    if ($this->debug_mode) {
      drupal_set_message(t('Request URL: !url', array('!url' => urldecode($request_url))));
    }
    
    // Pass the URL to the request method
    return $this->request($request_url);
  }

  /**
   * Actual HTTP request.
   */
  private function request($request_url) {
    $response = drupal_http_request($request_url);
    if ($response->code != 200) {
      watchdog('shanti_kmaps_views', 'HTTP error !code received.', array('!code' => $response->code), WATCHDOG_ERROR);
      return FALSE;
    }
    $data = json_decode($response->data);
    if (!is_object($data)) {
      watchdog('shanti_kmaps_views', 'Did not receive valid API response (invalid JSON).', array(), WATCHDOG_ERROR);
      return FALSE;
    }
    if (isset($data->error)) {
      watchdog('shanti_kmaps_views', 'Error !code received: %message', array('!code' => $data->error, '%message' => $data->message), WATCHDOG_ERROR);
      return FALSE;
    }
    
    // Parse the data before returning
    return $this->parse($data);
  }

  /**
   * Parses the response data. Return format depends on parse_mode and fetch_mode, since
   * fetch_modes may return slightly different JSON structures :( 
   */
  private function parse($data) {
    $parsed_data = array();
    // Autocomplete expexted a simpke id/label hash that will be converted to JSON
    // May need to specify a parse_mode for views as well
    if ($this->parse_mode == 'autocomplete') {
      if ($this->fetch_mode == 'by_name') {
        foreach ($data->features as $item) {
          $parsed_data[$item->id] = $item->header;
        }        
      } elseif ($this->fetch_mode == 'by_fid') {
        foreach ($data->features as $item) {
          $parsed_data[$item->id] = $item->header;
          // Add othe stuff too
        }                
      } elseif ($this->fetch_mode == 'by_fid_cached') {
        // We append an ancestor path to provide context for the item
        $ancestors = array();
        foreach ($data->feature->ancestors as $ancestor) { $ancestors[] = $ancestor->header; }
        array_pop($ancestors); // Remove the last ancestor, which is the current item
        $parsed_data[$data->feature->id] = $data->feature->header . '|' . implode('/', $ancestors);        
      }    
    // Raw mode is for other purposes
    } elseif ($this->parse_mode == 'raw') {
      $parsed_data = $data;
    } else {
      // Do nothing for now.
    }
    
    return $parsed_data;
  }
  
  // SPECIFIC REQUESTS
  
  public function searchByName($search_string) { 
    $this->setString($search_string);
    $this->addArgument('per_page', $this->results_per_page);
    $this->setFetchModeToName();
    $this->setFormatToJSON();
    $this->setParseModeToAutocomplete();
    $parsed_data = $this->execute();
    // When multiple IDs are fixed, then use the kmap_ids function and call non-cache URL
    $result = array();
    $this->setFetchModeToIDCached(); // Now switch to fetching each ID
    foreach ($parsed_data as $kmap_id => $kmap_label) {
      $r = $this->setKMapID($kmap_id)->execute();
      $key = "$kmap_id|{$r[$kmap_id]}"; // Not sure if it's best to do this here
      $result[$key] = $r[$kmap_id];
    }
    return $result;
  }
    
}

