function New-SyntheticResponse($Payload) {
  $corpus=$Payload.messages[-1].content|ConvertFrom-Json
  $suggestions=@();$decision='no_change'
  if($Payload.model -eq 'weasel-synthetic' -and $corpus.target -ceq '这里有错子。'){
    $decision='suggestions';$suggestions=@(@{original='错子';replacement='错字';reason='固定合成测试响应';score=0.99})
  }
  $answer=@{version=1;decision=$decision;suggestions=$suggestions}|ConvertTo-Json -Depth 6 -Compress
  return (@{choices=@(@{finish_reason='stop';message=@{role='assistant';content=$answer}})}|ConvertTo-Json -Depth 8 -Compress)
}
