<?php
/**
 * @file
 * Datastream regeneration utilities.
 */

/**
 * Queries a collection for its configured content models.
 *
 * @param AbstractObject $collection
 *   The collection to query.
 *
 * @return array
 *   An array of content models.
 */
function islandora_batch_derivative_trigger_get_child_content_models(AbstractObject $collection) {
  $model_query = islandora_basic_collection_get_query_info(array(
    'object' => $collection,
    'page_size' => -1,
    'page_number' => 0,
    'vars' => '?model',
    'order_by' => FALSE,
  ));
  $models = (array) $collection->repository->ri->query($model_query['query'], $model_query['type']);
  $model_array = array();
  foreach ($models as $model) {
    $model_pid = $model['model']['value'];
    if ($model_pid === 'fedora-system:FedoraObject-3.0') {
      continue;
    }
    $cmodel = islandora_object_load($model_pid);
    $label = isset($cmodel->label) ? $cmodel->label : t("unknown type");
    $model_array[$model_pid] = array(
      'pid' => $model_pid,
      'label' => $label,
    );
  }
  return $model_array;
}

/**
 * A Drupal form select table populated with the given collection's children.
 *
 * This is almost identical to the function in islandora_basic_collection's
 * children select table generator, except that it can filter based on the
 * content model, and is able to return a pager useful for continued content
 * model filtration.
 *
 * @param AbstractObject $object
 *   The collection object.
 * @param array $pager_options
 *   An associative array containing options to pass onto the pager.
 *   - element: The numeric ID of the pager
 *   - fragment: The url fragment to append onto pager links, includes '#'.
 * @param string|null $content_model
 *   A content model to filter children by. Use NULL for all content models.
 *
 * @return array
 *   A Drupal select table form element.
 */
function islandora_batch_derivative_trigger_get_children_select_table_form_element_with_content_model(AbstractObject $object, array $pager_options, $content_model = NULL) {
  // Assumes all results are returned although the function description
  // states otherwise.
  $limit = 10;
  $page = pager_find_page($pager_options['element']);
  list($count, $results) = islandora_basic_collection_get_member_objects($object, $page, $limit, 'manage', $content_model);
  $page = pager_default_initialize($count, $limit, $pager_options['element']);
  $rows = array();
  foreach ($results as $result) {
    $pid = $result['object']['value'];
    $label = empty($result['title']['value']) ? t('Untitled') : filter_xss($result['title']['value']);
    $owner = empty($result['owner']['value']) ? t('Unowned') : filter_xss($result['owner']['value']);
    $date_modified = empty($result['date_modified']['value']) ? t('Unknown') : filter_xss($result['date_modified']['value']);
    $rows[$pid] = array(
      'owner' => array(
        'data' => array(
          '#markup' => '<div>' . $owner . '</div>',
        ),
      ),
      'date_modified' => array(
        'data' => array(
          '#markup' => '<div>' . $date_modified . '</div>',
        ),
      ),
      'label' => array(
        'data' => array(
          '#type' => 'link',
          '#title' => $label,
          '#href' => "islandora/object/{$pid}")));
  }
  // Theme pager doesn't support url fragments in D7 so we insert manually.
  $pager = theme('pager', array('quantity' => 10, 'element' => $pager_options['element']));
  $pattern = '/href="([^"]+)"/';
  $replace = $content_model ? 'href="\1' . "&content_model={$content_model}{$pager_options['fragment']}" . '"' : 'href="\1' . $pager_options['fragment'] . '"';
  $pager = preg_replace($pattern, $replace, $pager);
  // Keep the AJAX path out of the pager.
  if ($content_model && islandora_object_load($content_model)) {
    $pattern = '/system\/ajax/';
    $replace = "islandora/object/{$object->id}/manage/collection";
    $pager = preg_replace($pattern, $replace, $pager);
  }
  return array(
    '#type' => 'tableselect',
    '#header' => array(
      'label' => array('data' => t('Include only these items')),
      'owner' => array('data' => t('Owner')),
      'date_modified' => array('data' => t('Date Modified'))),
    '#js_select' => FALSE,
    '#options' => $rows,
    '#empty' => t('No children found for the selected content model.'),
    '#prefix' => $pager,
    '#suffix' => $pager,
  );
}

/**
 * Gets a collection's member objects.
 *
 * The islandora_basic_collection_get_member_objects() function has a lot of
 * overhead we don't need, and can only provide counts for slices. Recreating
 * it here, minus the bits we don't use.
 *
 * @param string $collection
 *   The PID of the collection.
 * @param string $content_model
 *   The content model of the children to get.
 * @param array|null $slice
 *   An array containing 'date' and 'pid' as slice parameters to offset by, or
 *   NULL to omit.
 * @param int $limit
 *   The number of results to return. Ignored for count queries.
 * @param bool $count
 *   Whether to perform a countQuery() this time around.
 *
 * @return array|int
 *   A Tuque RepositoryQuery::sparqlQuery() results array with 'created' and
 *   'object' as the createdDate and PID of the object, or a count integer if
 *   $count was TRUE.
 */
function islandora_batch_derivative_trigger_get_member_objects($collection, $content_model, $slice, $limit = 10, $count = FALSE) {
  // Build our query.
  $query = <<<EOQ
PREFIX xs: <xml-schema:>
SELECT DISTINCT ?object ?created
FROM <#ri>
WHERE {
  ?object <fedora-model:hasModel> <info:fedora/$content_model> ;
          <fedora-rels-ext:isMemberOfCollection> <info:fedora/$collection> ;
          <fedora-model:createdDate> ?created .
  !filter
}
!order
EOQ;
  if (is_array($slice)) {
    $filter = "FILTER(?created > '{$slice['date']}'^^xs:dateTime || (?created = '{$slice['date']}'^^xs:dateTime && xs:string(?object) > xs:string('info:fedora/{$slice['pid']}')))";
  }
  else {
    $filter = '';
  }
  $formatted_query = format_string($query, array(
    '!filter' => $filter,
    '!order' => $count ? '' : "ORDER BY ASC(?created) ASC(?object)",
  ));

  // Tuquetime.
  $connection = islandora_get_tuque_connection();
  // Get the count if that's what we were asked.
  if ($count) {
    return $connection->repository->ri->countQuery($formatted_query, 'sparql');
  }
  return $connection->repository->ri->sparqlQuery($formatted_query, $limit);
}
